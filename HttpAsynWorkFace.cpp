#include "HttpAsynWorkFace.h"
#include "HttpAsynWork.h"

HttpAsynWorkFace::HttpAsynWorkFace(QObject *parent)
    : QObject{parent}
{}

HttpAsynWorkFace::~HttpAsynWorkFace()
{

}

void HttpAsynWorkFace::initInterFace()
{
    HttpAsynWork::getInstance();
}

void HttpAsynWorkFace::submitRequest(RequestMethod method, const QString &url, const ResponseCallback &successCallback, const ErrorCallback &errorCallback, const QVariantMap &body, QObject *context)
{
    HttpAsynWork::getInstance()->submitRequest(static_cast<HttpAsynWork::RequestMethod>(method),url,successCallback,errorCallback,body,context);
}

void HttpAsynWorkFace::setMaxConcurrentRequests(int max)
{
    HttpAsynWork::getInstance()->setMaxConcurrentRequests(max);
}

void HttpAsynWorkFace::setBaseUrl(const QString& baseUrl)
{
    HttpAsynWork::getInstance()->setBaseUrl(baseUrl);
}

void HttpAsynWorkFace::setRequestTimeout(int milliseconds)
{
    HttpAsynWork::getInstance()->setRequestTimeout(milliseconds);
}

void HttpAsynWorkFace::setHeaders(const QVariantMap heads)
{
    HttpAsynWork::getInstance()->setHeaders(heads);
}
