#pragma once
#include "data/DataStructs.hpp"
#include <nlohmann/json.hpp>
#include <string>

class ResponseBuilder
{
  private:
    nlohmann::json response = {{"header", nlohmann::json::object()}, {"body", nlohmann::json::object()}};

  public:
    ResponseBuilder()
    {
        response["header"] = nlohmann::json::object(); // Ensure "header" is a JSON object
        response["body"] = nlohmann::json::object();   // Ensure "body" is a JSON object
    }

    template <typename T>
    ResponseBuilder &setHeader(const std::string &key, const T &value)
    {
        response["header"][key] = value;
        return *this;
    }

    template <typename T>
    ResponseBuilder &setBody(const std::string &key, const T &value)
    {
        response["body"][key] = value;
        return *this;
    }

    /**
     * @brief Set timestamp information in the response
     * @param timestamps TimestampStruct containing lag compensation data
     * @return Reference to this builder for method chaining
     */
    ResponseBuilder &setTimestamps(const TimestampStruct &timestamps)
    {
        response["header"]["serverRecvMs"] = timestamps.serverRecvMs;
        response["header"]["serverSendMs"] = timestamps.serverSendMs;
        response["header"]["clientSendMsEcho"] = timestamps.clientSendMsEcho;
        response["header"]["requestId"] = timestamps.requestId;
        return *this;
    }

    nlohmann::json build()
    {
        return response; // returns the built JSON object
    }
};