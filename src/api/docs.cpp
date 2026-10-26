/**
 * @file
 * @brief Serve build-time embedded documentation through Drogon.
 */

#include "api/docs.h"
#include "api_docs_assets.h"
#include <drogon/HttpAppFramework.h>
#include <drogon/HttpResponse.h>
#include <functional>

void api::setupDocumentation() {
   drogon::app().registerHandler(
      "/openapi.json",
      [](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
         auto response = drogon::HttpResponse::newHttpResponse();
         response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
         response->setBody(assets::openapi);
         callback(response);
      },
      {drogon::Get}, "OpenApiSpecification");

   const auto swaggerUi = [](const drogon::HttpRequestPtr&,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
      auto response = drogon::HttpResponse::newHttpResponse();
      response->setContentTypeCode(drogon::CT_TEXT_HTML);
      response->setBody(assets::swaggerUi);
      callback(response);
   };
   drogon::app().registerHandler("/docs", swaggerUi, {drogon::Get}, "SwaggerUi");
   drogon::app().registerHandler("/docs/", swaggerUi, {drogon::Get}, "SwaggerUiSlash");
}
