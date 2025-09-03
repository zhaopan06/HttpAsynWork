#include "HttpAsynWork.h"
#include "qtimer.h"
#include "qurlquery.h"
#include <QNetworkReply>
#include <QDebug>
#include <QPointer>
#include <QSslConfiguration>
#include <QJsonDocument>

HttpAsynWork* HttpAsynWork::getInstance()
{
    static HttpAsynWork instance;
    return &instance;
}

HttpAsynWork::HttpAsynWork(QObject* parent)
    : QObject(parent),
    m_manager(new QNetworkAccessManager(this)),
    m_activeRequests(0),
    m_maxConcurrentRequests(4),
    m_requestTimeout(30000)
{
    moveToThread(&m_workerThread);
    m_manager->moveToThread(&m_workerThread);
    m_workerThread.start();

    connect(this, &HttpAsynWork::requestAdded, this, &HttpAsynWork::handleRequest, Qt::QueuedConnection);
}

HttpAsynWork::~HttpAsynWork()
{
    m_manager->deleteLater();
    m_workerThread.quit();
    m_workerThread.wait();
}

void HttpAsynWork::submitRequest(RequestMethod method, const QString& url,
                                 const ResponseCallback& successCallback,
                                 const ErrorCallback& errorCallback,
                                 const QVariantMap &body,
                                 QObject* context)
{
    QMutexLocker locker(&m_queueMutex);

    RequestTask task;
    task.method = method;
    task.url = url;
    task.body = body;
    task.successCallback = successCallback;
    task.errorCallback = errorCallback;
    task.context = context ? context : nullptr;
    task.timeout = m_requestTimeout;

    m_requestQueue.enqueue(task);
    emit requestAdded();
}

void HttpAsynWork::handleRequest()
{
    QMutexLocker locker(&m_queueMutex);

    if (m_requestQueue.isEmpty() || m_activeRequests >= m_maxConcurrentRequests)
    {
        return;
    }

    RequestTask task = m_requestQueue.dequeue();
    m_activeRequests++;

    QByteArray body = QJsonDocument::fromVariant(task.body).toJson();
    QString fullUrl = m_baseUrl + task.url;
    QNetworkRequest request = createRequest(fullUrl);

    QSharedPointer<QTimer> timeoutTimer(new QTimer);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(task.timeout);

    QNetworkReply* reply = nullptr;
    switch (task.method)
    {
    case RequestMethod::GET:
    {
        QNetworkRequest request = createRequest(fullUrl, task.body);
        reply = m_manager->get(request);
        break;
    }
    case RequestMethod::POST:
        reply = m_manager->post(request, body);
        break;
    case RequestMethod::PUT:
        reply = m_manager->put(request, body);
        break;
    case RequestMethod::PATCH:
        reply = m_manager->sendCustomRequest(request, "PATCH", body);
        break;
    }

    QPointer<QObject> targetContext = QPointer<QObject>(task.context);

    if (reply)
    {
        connect(timeoutTimer.data(), &QTimer::timeout, this, [=]() {
            reply->abort();
            if(targetContext)
            {
                if(task.errorCallback)
                {
                    QMetaObject::invokeMethod(targetContext, [=]() {
                        QString toastMsg = QStringLiteral("请求超时");
                        task.errorCallback(-1,toastMsg);
                    }, Qt::QueuedConnection);
                }
            }
        });

        timeoutTimer->start();
        QObject::connect(reply, &QNetworkReply::finished, reply, [=]{

            timeoutTimer->stop();

            QByteArray response = reply->readAll();
            QJsonParseError json_error;
            QJsonDocument jsonDocument = QJsonDocument::fromJson(response, &json_error);

            if (reply->error() != QNetworkReply::NoError)
            {
                if(targetContext)
                {
                    if(task.errorCallback)
                    {
                        QMetaObject::invokeMethod(targetContext, [=]() {
                            const int errorCode = reply->error();
                            const QString errorMsg = QStringLiteral("网络错误");
                            task.errorCallback(errorCode,errorMsg);
                        }, Qt::QueuedConnection);
                    }
                }
            }
            else if(json_error.error != QJsonParseError::NoError)
            {
                if(targetContext)
                {
                    if(task.errorCallback)
                    {
                        QMetaObject::invokeMethod(targetContext, [=]() {
                            const QString errorMsg = QStringLiteral("json错误，请重新请求");
                            const auto errorCallback = task.errorCallback;
                            errorCallback(-1,  errorMsg);
                        }, Qt::QueuedConnection);
                    }
                }
            }
            else if(jsonDocument["code"].toInt() != 1)
            {
                if(targetContext)
                {
                    if(task.errorCallback)
                    {
                        QMetaObject::invokeMethod(targetContext, [=]() {
                            task.errorCallback(jsonDocument["code"].toInt(),jsonDocument["message"].toString());
                        }, Qt::QueuedConnection);
                    }
                }
            }
            else if(task.successCallback)
            {
                if(targetContext)
                {
                    QMetaObject::invokeMethod(targetContext, [=]() {
                        task.successCallback(jsonDocument.toVariant().toMap());
                    }, Qt::QueuedConnection);
                }
            }

            reply->deleteLater();
            m_activeRequests--;

            if(m_activeRequests > 0)
                QMetaObject::invokeMethod(this, &HttpAsynWork::handleRequest, Qt::QueuedConnection);
        });
    }
    else
    {
        m_activeRequests--;
        if(targetContext)
        {
            if(task.errorCallback)
            {
                QMetaObject::invokeMethod(targetContext, [=]() {
                    const int errorCode = reply->error();
                    const QString errorMsg = QStringLiteral("网络错误");
                    task.errorCallback(errorCode,errorMsg);
                }, Qt::QueuedConnection);
            }
        }

        if(m_activeRequests > 0)
            QMetaObject::invokeMethod(this, &HttpAsynWork::handleRequest, Qt::QueuedConnection);
    }
}

QNetworkRequest HttpAsynWork::createRequest(const QString& url, const QVariantMap &body)
{
    QUrlQuery query;
    for (auto it = body.constBegin(); it != body.constEnd(); ++it)
    {
        query.addQueryItem(it.key(), it.value().toString());
    }
    QUrl qurl(url);
    if(!body.isEmpty())
        qurl.setQuery(query);

    QNetworkRequest request;
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setUrl(qurl);

    if (url.startsWith("https://", Qt::CaseInsensitive))
    {
        QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
        sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
        sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
        request.setSslConfiguration(sslConfig);
    }

    for (auto it = m_heads.constBegin(); it != m_heads.constEnd(); ++it)
    {
        request.setRawHeader(it.key().toUtf8(), it.value().toByteArray());
    }

    request.setRawHeader("token", m_token.isEmpty() ? "0" : m_token.toLatin1());
    return request;
}

void HttpAsynWork::setMaxConcurrentRequests(int max)
{
    QMutexLocker locker(&m_queueMutex);
    m_maxConcurrentRequests = qMax(1, max);
}

void HttpAsynWork::setBaseUrl(const QString& baseUrl)
{
    QMutexLocker locker(&m_queueMutex);
    m_baseUrl = baseUrl;
}

void HttpAsynWork::setRequestTimeout(int milliseconds)
{
    QMutexLocker locker(&m_queueMutex);
    m_requestTimeout = qMax(1000, milliseconds);
}

void HttpAsynWork::setHeaders(const QVariantMap heads)
{
    QMutexLocker locker(&m_queueMutex); 
    m_heads = heads;
}

void HttpAsynWork::setToken(QString token)
{
    QMutexLocker locker(&m_queueMutex);
    m_token = token;
}
