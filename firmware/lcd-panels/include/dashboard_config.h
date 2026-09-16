#pragma once

#include <Arduino.h>

#include "dashboard_types.h"

bool dashboardConfigValid(const DashboardConfig& value);
bool dashboardLoadConfig(const char* key, DashboardConfig& value);
bool dashboardStoreConfig(const char* key, const DashboardConfig& value);
bool dashboardRemoveConfig(const char* key);
bool dashboardPromotePending(const DashboardConfig& value);
bool dashboardPendingSuppressed();
void dashboardBeginPendingValidation();
void dashboardRejectPending();
bool dashboardRecoveryRequested();
bool dashboardStartProvisioning();
bool dashboardProvisioningActive();
void dashboardHandleProvisioning();
