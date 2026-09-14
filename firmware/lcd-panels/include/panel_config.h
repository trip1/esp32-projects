#pragma once

#include <Arduino.h>

#include "panel_logic.h"

bool panelLoadConfig(const char* key, PanelConfig& value);
bool panelStoreConfig(const char* key, const PanelConfig& value);
bool panelRemoveConfig(const char* key);
bool panelPromotePending(const PanelConfig& value);
bool panelPendingSuppressed();
void panelBeginPendingValidation();
void panelRejectPending();
bool panelRecoveryRequested();
bool panelStartProvisioning();
bool panelProvisioningActive();
void panelHandleProvisioning();
