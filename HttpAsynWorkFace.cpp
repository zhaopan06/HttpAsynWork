#include "HttpAsynWorkFace.h"
#include "HttpAsyncWorker.h"

HttpAsynWorkFace::HttpAsynWorkFace()
    : QObject{nullptr}
{}

HttpAsynWorkFace::~HttpAsynWorkFace()
{

}

void HttpAsynWorkFace::initInterFace()
{
    HttpAsyncWorker::getInstance();
}

void HttpAsynWorkFace::submitRequest(RequestMethod method, const QString &url, const ResponseCallback &successCallback, const ErrorCallback &errorCallback, const QVariantMap &body, QObject *context)
{
    HttpAsyncWorker::getInstance()->submitRequest(static_cast<HttpAsyncWorker::RequestMethod>(method),url,successCallback,errorCallback,body,context);
}

void HttpAsynWorkFace::setMaxConcurrentRequests(int max)
{
    HttpAsyncWorker::getInstance()->setMaxConcurrentRequests(max);
}

void HttpAsynWorkFace::setBaseUrl(const QString& baseUrl)
{
    HttpAsyncWorker::getInstance()->setBaseUrl(baseUrl);
}

void HttpAsynWorkFace::setRequestTimeout(int milliseconds)
{
    HttpAsyncWorker::getInstance()->setRequestTimeout(milliseconds);
}

void HttpAsynWorkFace::setHeaders(const QVariantMap heads)
{
    HttpAsyncWorker::getInstance()->setHeaders(heads);
}

void HttpAsynWorkFace::setToken(QString token)
{
    HttpAsyncWorker::getInstance()->setToken(token);
}
