#include "utils/TimestampUtils.hpp"
#include <sstream>

long long
TimestampUtils::getCurrentTimestampMs()
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

void
TimestampUtils::setServerReceiveTimestamp(TimestampStruct &timestamps)
{
    timestamps.serverRecvMs = getCurrentTimestampMs();
}

void
TimestampUtils::setServerSendTimestamp(TimestampStruct &timestamps)
{
    timestamps.serverSendMs = getCurrentTimestampMs();
}

TimestampStruct
TimestampUtils::parseTimestampsFromHeader(const nlohmann::json &json)
{
    TimestampStruct timestamps;

    try
    {
        if (json.contains("header"))
        {
            const auto &header = json["header"];

            if (header.contains("clientSendMs"))
            {
                timestamps.clientSendMsEcho = header["clientSendMs"].get<long long>();
            }

            if (header.contains("requestId"))
            {
                timestamps.requestId = header["requestId"].get<std::string>();
            }
        }
    }
    catch (const std::exception &e)
    {
        // If parsing fails, return default timestamps
        timestamps = TimestampStruct{};
    }

    // Set receive timestamp to current time
    setServerReceiveTimestamp(timestamps);

    return timestamps;
}

void
TimestampUtils::addTimestampsToHeader(nlohmann::json &json, const TimestampStruct &timestamps)
{
    if (!json.contains("header"))
    {
        json["header"] = nlohmann::json::object();
    }

    json["header"]["serverRecvMs"] = timestamps.serverRecvMs;
    json["header"]["serverSendMs"] = timestamps.serverSendMs;
    json["header"]["clientSendMsEcho"] = timestamps.clientSendMsEcho;
    json["header"]["requestId"] = timestamps.requestId;
}

TimestampStruct
TimestampUtils::createTimestamp()
{
    TimestampStruct timestamps;
    setServerReceiveTimestamp(timestamps);
    return timestamps;
}

TimestampStruct
TimestampUtils::parseTimestampsFromBuffer(const std::array<char, 1024> &messageBuffer, size_t messageLength)
{
    TimestampStruct timestamps;

    try
    {
        // Convert buffer to string
        std::string messageStr(messageBuffer.data(), messageLength);

        // Parse JSON
        nlohmann::json json = nlohmann::json::parse(messageStr);

        // Parse timestamps from header
        timestamps = parseTimestampsFromHeader(json);
    }
    catch (const std::exception &e)
    {
        // If parsing fails, create default timestamp with current receive time
        timestamps = createTimestamp();
    }

    return timestamps;
}
