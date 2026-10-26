/**
 * @file
 * @brief Registration of embedded OpenAPI and Swagger UI endpoints.
 */

#pragma once

/** @brief HTTP-facing integration and embedded API documentation. */
namespace api {
/**
 * @brief Register GET /openapi.json, /docs, and /docs/ before starting Drogon.
 *
 * Assets are embedded by CMake, so serving them does not require filesystem access.
 * Swagger UI loads its pinned JavaScript and CSS from jsDelivr in the browser;
 * its API requests target this server. The JSON specification itself works offline.
 * @pre Register once, before drogon::app().run().
 */
void setupDocumentation();
} // namespace api
