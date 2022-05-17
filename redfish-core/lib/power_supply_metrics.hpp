#pragma once

namespace redfish
{

using averageMaxEntry = std::tuple<std::uint64_t, std::int64_t>;
using historyEntry = std::tuple<std::uint64_t, std::int64_t, std::int64_t>;
using averageMaxArray = std::vector<averageMaxEntry>;
using historyArray = std::vector<historyEntry>;

/**
 * @brief Get power supply average and date values given chassis and power
 * supply IDs.
 *
 * @param[in] aResp - Shared pointer for asynchronous calls.
 * @param[in] chassisID - Chassis to which the values are associated.
 * @param[in] powerSupplyID - Power supply to which the values are associated.
 *
 * @return None.
 */
inline void getValues(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                      const std::string& chassisID,
                      const std::string& powerSupplyID)
{
    BMCWEB_LOG_DEBUG
        << "Get power supply date/average/maximum input power values";
    // Setup InputPowerHistoryItem values array.
    // It will have 0 to many date/timestamp, average, and maximum entries.
    aResp->res.jsonValue["Oem"]["IBM"]["InputPowerHistoryItem"]["@odata.type"] =
        "#OemPowerSupplyMetric.InputPowerHistoryItem";
    nlohmann::json& jsonResponse = aResp->res.jsonValue;
    nlohmann::json& inputPowerHistoryItemArray =
        jsonResponse["Oem"]["IBM"]["InputPowerHistoryItem"];
    inputPowerHistoryItemArray = nlohmann::json::array();
    {
        BMCWEB_LOG_DEBUG << "Fake values";
        auto date = crow::utility::getDateTime(
            static_cast<std::time_t>((1652222513979 / 1000)));
        inputPowerHistoryItemArray.push_back(
            {{"Date", date}, {"Average", 25}, {"Maximum", 26}});
    }

    const std::string averageInterface =
        "org.open_power.Sensor.Aggregation.History.Average";
    const std::string maximumInterface =
        "org.open_power.Sensor.Aggregation.History.Maximum";

    // averageMaxArray averageValues;
    // getValues2(aResp, chassisID, powerSupplyID, averageInterface,
    //           averageValues);
    // averageMaxArray maximumValues;
    // getValues2(aResp, chassisID, powerSupplyID, maximumInterface,
    //           maximumValues);

    const std::array<const char*, 2> interfaces = {
        "org.open_power.Sensor.Aggregation.History.Average",
        "org.open_power.Sensor.Aggregation.History.Maximum"};

    crow::connections::systemBus->async_method_call(
        [aResp, chassisID, powerSupplyID](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                intfSubTree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "D-Bus response error on GetSubTree " << ec;
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& [objectPath, connectionNames] : intfSubTree)
            {
                if (objectPath.empty())
                {
                    BMCWEB_LOG_DEBUG << "Error getting D-Bus object!";
                    messages::internalError(aResp->res);
                    return;
                }

                const std::string& validPath = objectPath;
                auto psuMatchStr = powerSupplyID + "_input_power";
                std::string psuInputPowerStr;
                // validPath -> {psu}_input_power
                // 5 below comes from
                // /org/open_power/sensors/aggregation/per_30s/
                //   0      1         2         3         4
                // .../{psu}_input_power/[average,maximum]
                //           5..............6
                if (!dbus::utility::getNthStringFromPath(validPath, 5,
                                                         psuInputPowerStr))
                {
                    BMCWEB_LOG_ERROR << "Got invalid path " << validPath;
                    messages::invalidObject(aResp->res, validPath);
                    return;
                }

                if (psuInputPowerStr != psuMatchStr)
                {
                    // not this power supply, continue to next objectPath...
                    continue;
                }

                BMCWEB_LOG_DEBUG << "Got validPath: " << validPath;
                for (const auto& connection : connectionNames)
                {
                    auto serviceName = connection.first;

                    for (const auto& interfaceName : connection.second)
                    {
                        // any_of(interfaces) ?
                        if ((interfaceName == "org.open_power.Sensor."
                                              "Aggregation.History.Average") ||
                            (interfaceName == "org.open_power.Sensor."
                                              "Aggregation.History.Maximum"))
                        {
                            BMCWEB_LOG_DEBUG
                                << "Get Values from serviceName: "
                                << serviceName << " objectPath: " << validPath
                                << " interfaceName: " << interfaceName;

                            crow::connections::systemBus->async_method_call(
                                [aResp, serviceName, validPath, interfaceName](
                                    const boost::system::error_code ec2,
                                    const std::variant<averageMaxArray>&
                                        intfValues) {
                                    if (ec2)
                                    {
                                        BMCWEB_LOG_DEBUG
                                            << "D-Bus response error";
                                        messages::internalError(aResp->res);
                                        return;
                                    }
                                    const averageMaxArray* intfValuesPtr =
                                        std::get_if<averageMaxArray>(
                                            &intfValues);
                                    if (intfValuesPtr == nullptr)
                                    {
                                        messages::internalError(aResp->res);
                                        return;
                                    }

                                    for (const auto& values : *intfValuesPtr)
                                    {
                                        // The first value returned is the
                                        // timestamp, it is in milliseconds
                                        // since the Epoch. Divide that by 1000,
                                        // to get date/time via seconds since
                                        // the Epoch.
                                        BMCWEB_LOG_DEBUG
                                            << "DateTime: "
                                            << crow::utility::getDateTime(
                                                   static_cast<std::time_t>(
                                                       (std::get<0>(values) /
                                                        1000)));
                                        // The second value returned is in
                                        // watts. It is the average watts this
                                        // power supply has used in a 30 second
                                        // interval.
                                        BMCWEB_LOG_DEBUG << "Values: "
                                                         << std::get<1>(values);
                                    }
                                    //...
                                },
                                serviceName, validPath,
                                "org.freedesktop.DBus.Properties", "Get",
                                interfaceName, "Values");
                        }
                    }
                }
            // BMCWEB_LOG_DEBUG << "Got serviceName: " << serviceName;
#if 0
                const std::string& connectionName = serviceName[0].first;
                // validPath -> {psu}_input_power
                // 5 below comes from
                // /org/open_power/sensors/aggregation/per_30s/
                //   0      1         2         3         4
                // .../{psu}_input_power/[average,maximum]
                //           5..............6
                // I know it is average... I need to match the psu?
                auto psuMatchStr = powerSupplyID + "_input_power";
                std::string psuInputPowerStr;

                if (!dbus::utility::getNthStringFromPath(validPath, 5,
                                                         psuInputPowerStr))
                {
                    BMCWEB_LOG_ERROR << "Got invalid path " << validPath;
                    messages::invalidObject(aResp->res, validPath);
                    return;
                }

                if (psuInputPowerStr == psuMatchStr)
                {
                    BMCWEB_LOG_DEBUG << "validPath " << validPath;
                    BMCWEB_LOG_DEBUG << "connectionName " << connectionName;

                    crow::connections::systemBus->async_method_call(
                        [aResp, chassisID, powerSupplyID](
                            const boost::system::error_code ec2,
                            const std::variant<averageMaxArray>& intfValues) {
                            if (ec2)
                            {
                                BMCWEB_LOG_DEBUG << "D-Bus response error";
                                messages::internalError(aResp->res);
                                return;
                            }
                            const averageMaxArray* intfValuesPtr =
                                std::get_if<averageMaxArray>(&intfValues);
                            if (intfValuesPtr == nullptr)
                            {
                                messages::internalError(aResp->res);
                                return;
                            }

                            for (const auto& values : *intfValuesPtr)
                            {
                                // The first value returned is the
                                // timestamp, it is in milliseconds
                                // since the Epoch. Divide that by 1000,
                                // to get date/time via seconds since
                                // the Epoch.
                                BMCWEB_LOG_DEBUG
                                    << "DateTime: "
                                    << crow::utility::getDateTime(
                                           static_cast<std::time_t>(
                                               (std::get<0>(values) / 1000)));
                                // The second value returned is in
                                // watts. It is the average watts this
                                // power supply has used in a 30 second
                                // interval.
                                BMCWEB_LOG_DEBUG << "Values: "
                                                 << std::get<1>(values);
                            }
                            //...
                        },
                        connectionName, validPath,
                        "org.freedesktop.DBus.Properties", "Get", interfaceName,
                        "Values");
                }
#endif
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/org/open_power/sensors/aggregation/per_30s", 0, interfaces);
}

/**
 * Systems derived class for delivering OemPowerSupplyMetrics Schema.
 */
inline void requestRoutesPowerSupplyMetrics(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/PowerSubsystem/"
                      "PowerSupplies/<str>/Metrics")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& chassisID, const std::string& powerSupplyID) {
                // const std::string& chassisId = param;
                auto getChassisID =
                    [asyncResp, chassisID, powerSupplyID](
                        const std::optional<std::string>& validChassisID) {
                        if (!validChassisID)
                        {
                            BMCWEB_LOG_ERROR << "Not a valid chassis ID:"
                                             << chassisID;
                            messages::resourceNotFound(asyncResp->res,
                                                       "Chassis", chassisID);
                            return;
                        }

                        // const std::string& powerSupplyId = param2;

                        BMCWEB_LOG_DEBUG << "ChassisID: " << chassisID;
                        BMCWEB_LOG_DEBUG << "PowerSupplyID: " << powerSupplyID;

                        asyncResp->res.jsonValue["@odata.type"] =
                            "#PowerSupplyMetrics.v1_0_0.PowerSupplyMetrics";
                        asyncResp->res.jsonValue["@odata.id"] =
                            "/redfish/v1/Chassis/" + chassisID +
                            "/PowerSubsystem/PowerSupplies/" + powerSupplyID +
                            "/Metrics";
                        asyncResp->res.jsonValue["Name"] =
                            "Metrics for " + powerSupplyID;
                        asyncResp->res.jsonValue["Id"] = "Metrics";

                        asyncResp->res.jsonValue["Oem"]["@odata.type"] =
                            "#OemPowerSupplyMetrics.Oem";
                        asyncResp->res.jsonValue["Oem"]["IBM"]["@odata.type"] =
                            "#OemPowerSupplyMetrics.IBM";
                        getValues(asyncResp, chassisID, powerSupplyID);
                        // getMaxValues(asyncResp, chassisID, powerSupplyID);
                    };
                redfish::chassis_utils::getValidChassisID(
                    asyncResp, chassisID, std::move(getChassisID));
            });
}
} // namespace redfish
