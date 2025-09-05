#ifndef HTTPASYNCWORKER_H
#define HTTPASYNCWORKER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QQueue>
#include <QMutex>
#include <QThread>
#include <QMap>
#include <QVariantMap>
#include <functional>

class HttpAsyncWorker : public QObject
{
    Q_OBJECT
public:
    enum class RequestMethod {
        GET,
        POST,
        PUT,
        PATCH
    };

    using ResponseCallback = std::function<void(const QVariant&)>;
    using ErrorCallback = std::function<void(int, const QString&)>;

    struct RequestTask {
        RequestMethod method;
        QString url;
        QVariantMap body;
        QObject* context = nullptr;  // 回调执行的上下文对象
        ResponseCallback successCallback;
        ErrorCallback errorCallback;
        int timeout = 30000;  // 请求超时时间(毫秒)
    };

    static HttpAsyncWorker* getInstance();
    ~HttpAsyncWorker();

    // 提交请求接口
    void submitRequest(RequestMethod method,
                       const QString& url,
                       const ResponseCallback& successCallback,
                       const ErrorCallback& errorCallback = nullptr,
                       const QVariantMap& body = QVariantMap(),
                       QObject* context = nullptr);

    // 配置接口
    void setMaxConcurrentRequests(int max);
    void setBaseUrl(const QString& baseUrl);
    void setRequestTimeout(int milliseconds);

    // 头部管理接口
    void setHeaders(const QVariantMap map);
    void addHeader(const QString& key, const QString& value);
    void removeHeader(const QString& key);
    void clearHeaders();
    void setToken(QString token);

signals:
    void requestAdded();

private:
    explicit HttpAsyncWorker(QObject* parent = nullptr);
    QNetworkRequest createRequest(const QString& url, const QVariantMap &body = QVariantMap());

    QNetworkAccessManager* m_manager;
    QQueue<RequestTask> m_requestQueue;
    QMutex m_queueMutex;    
    std::atomic<int> m_activeRequests{0};
    int m_maxConcurrentRequests = 4;
    int m_requestTimeout = 30000;  // 默认30秒超时
    QString m_baseUrl;
    QThread m_workerThread;
    QVariantMap m_map;  // 自定义头部
    QString m_token = "";
private slots:
    void handleRequest();
};

#endif // HTTPASYNCWORKER_H
