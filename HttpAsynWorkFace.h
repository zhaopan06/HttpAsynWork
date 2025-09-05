#ifndef HTTPASYNWORKFACE_H
#define HTTPASYNWORKFACE_H

#include <QObject>
#include <QVariantMap>

// 直接在主头文件中定义导出宏
#if defined(HTTPASYNWORK_LIBRARY)
#  define HTTPASYNWORK_EXPORT Q_DECL_EXPORT
#else
#  define HTTPASYNWORK_EXPORT Q_DECL_IMPORT
#endif

class HTTPASYNWORK_EXPORT  HttpAsynWorkFace : public QObject
{
    Q_OBJECT
public:
    HttpAsynWorkFace();
    ~HttpAsynWorkFace();

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

    //初始化
    void initInterFace();
    // 配置接口
    void setMaxConcurrentRequests(int max);
    void setBaseUrl(const QString& baseUrl);
    void setRequestTimeout(int milliseconds);

    // 头部管理接口
    void setHeaders(const QVariantMap heads);
    void setToken(QString token);

    // 提交请求接口
    void submitRequest(RequestMethod method,
                       const QString& url,
                       const ResponseCallback& successCallback,
                       const ErrorCallback& errorCallback = nullptr,
                       const QVariantMap& body = QVariantMap(),
                       QObject* context = nullptr);

};

#endif // HTTPASYNWORKFACE_H
