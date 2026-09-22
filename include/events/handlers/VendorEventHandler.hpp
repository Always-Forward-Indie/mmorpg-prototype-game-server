#pragma once

#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Vendor / trainer static-catalog handlers (JOIN entries "vendor",
 * "trainer").
 *
 * First domain extracted from the EventHandler god-object (proof of concept
 * for the handlers/ split). Pure reads: no outbox interaction.
 */
class VendorEventHandler : public BaseEventHandler
{
  public:
    VendorEventHandler(
        NetworkManager &networkManager,
        GameServices &gameServices);

    void handleGetVendorDataEvent(const Event &event);
    void handleGetTrainerDataEvent(const Event &event);
};
