#pragma once
#include "data/DataStructs.hpp"
#include <chrono>
#include <nlohmann/json.hpp>
#include <string>

/**
 * @brief Utility class for handling timestamp operations for lag compensation
 */
class TimestampUtils
{
  public:
    /**
     * @brief Get current timestamp in milliseconds since epoch
     * @return Current timestamp in milliseconds
     */
    static long long getCurrentTimestampMs();

    /**
     * @brief Set server receive timestamp to current time
     * @param timestamps Reference to TimestampStruct to update
     */
    static void setServerReceiveTimestamp(TimestampStruct &timestamps);

    /**
     * @brief Set server send timestamp to current time
     * @param timestamps Reference to TimestampStruct to update
     */
    static void setServerSendTimestamp(TimestampStruct &timestamps);

    /**
     * @brief Parse timestamps from JSON header
     * @param json JSON object containing header with timestamp information
     * @return Parsed TimestampStruct
     */
    static TimestampStruct parseTimestampsFromHeader(const nlohmann::json &json);

    /**
     * @brief Add timestamps to JSON header for response
     * @param json Reference to JSON object to add timestamps to
     * @param timestamps TimestampStruct containing timing information
     */
    static void addTimestampsToHeader(nlohmann::json &json, const TimestampStruct &timestamps);

    /**
     * @brief Create empty timestamp structure with current receive time
     * @return TimestampStruct with serverRecvMs set to current time
     */
    static TimestampStruct createTimestamp();

    /**
     * @brief Parse timestamps from message buffer
     * @param messageBuffer Buffer containing the message
     * @param messageLength Length of the message
     * @return Parsed TimestampStruct
     */
    static TimestampStruct parseTimestampsFromBuffer(const std::array<char, 1024> &messageBuffer, size_t messageLength);
};
