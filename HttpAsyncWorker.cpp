#include "HttpAsyncWorker.h"
#include <QNetworkReply>
#include <QDebug>
#include <QPointer>
#include "qtimer.h"
#include <QSslConfiguration>
#include <qurlquery.h>
#include <QJsonDocument>

HttpAsyncWorker* HttpAsyncWorker::getInstance()
{
    static HttpAsyncWorker instance;
    return &instance;
}

HttpAsyncWorker::HttpAsyncWorker(QObject* parent)
    : QObject(parent),
    m_manager(new QNetworkAccessManager(this)),
    m_activeRequests(0),
    m_maxConcurrentRequests(4),
    m_requestTimeout(30000)
{
    moveToThread(&m_workerThread);
    m_manager->moveToThread(&m_workerThread);
    m_workerThread.start();

    connect(this, &HttpAsyncWorker::requestAdded, this, &HttpAsyncWorker::handleRequest, Qt::QueuedConnection);
}

HttpAsyncWorker::~HttpAsyncWorker()
{
    m_workerThread.quit();
    m_workerThread.wait();
}

void HttpAsyncWorker::submitRequest(RequestMethod method, const QString& url,
                                    const ResponseCallback& successCallback,
                                    const ErrorCallback& errorCallback,
                                    const QVariantMap &body,
                                    QObject* context /* = nullptr */)
{
    QMutexLocker locker(&m_queueMutex);

    RequestTask task;
    task.method = method;
    task.url = url;
    task.body = body;
    task.successCallback = successCallback;
    task.errorCallback = errorCallback;
    task.context = context;
    task.timeout = m_requestTimeout;

    m_requestQueue.enqueue(task);
    emit requestAdded();
}

void HttpAsyncWorker::handleRequest()
{
    RequestTask task;
    bool hasTask = false;

    {
        QMutexLocker locker(&m_queueMutex);
        if (m_requestQueue.isEmpty() || m_activeRequests >= m_maxConcurrentRequests)
        {
            return;
        }
        task = m_requestQueue.dequeue();
        hasTask = true;
        m_activeRequests++;
    }

    if (!hasTask) {
        return;
    }

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

    QSharedPointer<std::atomic<bool>> requestCompleted(new std::atomic<bool>(false));
    QPointer<QObject> targetContext = QPointer<QObject>(task.context);

    if (reply)
    {
        // 设置超时处理
        connect(timeoutTimer.data(), &QTimer::timeout, [=]() {

            if (requestCompleted->load())
            {
                reply->deleteLater();
                return;
            }
            requestCompleted->store(true);

            reply->abort();
            reply->deleteLater();
            m_activeRequests--;

            if(task.errorCallback && !targetContext.isNull())
            {
                QMetaObject::invokeMethod(targetContext, [=]() {
                    QString errorMsg = QStringLiteral("请求超时");
                    task.errorCallback(-1, errorMsg);
                }, Qt::QueuedConnection);
            }

            if(m_activeRequests > 0)
                QMetaObject::invokeMethod(this, &HttpAsyncWorker::handleRequest, Qt::QueuedConnection);
        });

        timeoutTimer->start();
        QObject::connect(reply, &QNetworkReply::finished, reply, [=]{

            if (requestCompleted->load())
            {
                reply->deleteLater();
                return;
            }
            requestCompleted->store(true);

            m_activeRequests--;
            timeoutTimer->stop();

            QByteArray response = reply->readAll();
            QJsonParseError json_error;
            QJsonDocument jsonDocument = QJsonDocument::fromJson(response, &json_error);

            if (reply->error() != QNetworkReply::NoError)
            {
                if(task.errorCallback && !targetContext.isNull())
                {
                    QMetaObject::invokeMethod(targetContext, [=]() {
                        const int errorCode = reply->error();
                        const QString errorMsg = QStringLiteral("网络异常");
                        task.errorCallback(errorCode, errorMsg);
                    }, Qt::QueuedConnection);
                }
            }
            else if(json_error.error != QJsonParseError::NoError)
            {
                if(task.errorCallback && !targetContext.isNull())
                {
                    QMetaObject::invokeMethod(targetContext, [=]() {
                        const QString errorMsg = json_error.errorString();
                        task.errorCallback(-1, "JSON parse error: " + errorMsg);
                    }, Qt::QueuedConnection);
                }
            }
            else if(jsonDocument["code"].toInt() != 1)
            {
                if(task.errorCallback && !targetContext.isNull())
                {
                    QMetaObject::invokeMethod(targetContext, [=]() {
                        task.errorCallback(jsonDocument["code"].toInt(),jsonDocument["message"].toString());
                    }, Qt::QueuedConnection);
                }
            }
            else if(task.successCallback && !targetContext.isNull())
            {
                QMetaObject::invokeMethod(targetContext, [=]() {
                    task.successCallback(jsonDocument.toVariant().toMap());
                }, Qt::QueuedConnection);
            }

            reply->deleteLater();

            if(m_activeRequests > 0)
                QMetaObject::invokeMethod(this, &HttpAsyncWorker::handleRequest, Qt::QueuedConnection);
        });
    }
    else
    {
        m_activeRequests--;

        if(task.errorCallback && !targetContext.isNull())
        {
            QMetaObject::invokeMethod(targetContext, [=]() {
                QString errorMsg = "Failed to create request";
                task.errorCallback(-1, errorMsg);
            }, Qt::QueuedConnection);
        }

        if(m_activeRequests > 0)
            QMetaObject::invokeMethod(this, &HttpAsyncWorker::handleRequest, Qt::QueuedConnection);
    }
}

QNetworkRequest HttpAsyncWorker::createRequest(const QString& url, const QVariantMap &body)
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

    for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it)
    {
        request.setRawHeader(it.key().toUtf8(), it.value().toByteArray());
    }

    request.setRawHeader("token", m_token.isEmpty() ? "0" : m_token.toLatin1());
    return request;
}

void HttpAsyncWorker::setMaxConcurrentRequests(int max)
{
    QMutexLocker locker(&m_queueMutex);
    m_maxConcurrentRequests = qMax(1, max);
}

void HttpAsyncWorker::setBaseUrl(const QString& baseUrl)
{
    QMutexLocker locker(&m_queueMutex);
    m_baseUrl = baseUrl;
}

void HttpAsyncWorker::setRequestTimeout(int milliseconds)
{
    QMutexLocker locker(&m_queueMutex);
    m_requestTimeout = qMax(1000, milliseconds);
}

void HttpAsyncWorker::setHeaders(const QVariantMap map)
{
    QMutexLocker locker(&m_queueMutex);   
    m_map = map;
}

void HttpAsyncWorker::addHeader(const QString& key, const QString& value)
{
    QMutexLocker locker(&m_queueMutex);
    m_map.insert(key, value);
}

void HttpAsyncWorker::removeHeader(const QString& key)
{
    QMutexLocker locker(&m_queueMutex);
    m_map.remove(key);
}

void HttpAsyncWorker::setToken(QString token)
{
    QMutexLocker locker(&m_queueMutex);
    m_token = token;
}
