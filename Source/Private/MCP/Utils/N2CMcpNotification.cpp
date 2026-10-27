// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Utils/N2CMcpNotification.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Notifications/SNotificationList.h"

const FSlateBrush* FN2CMcpNotification::Icon(bool bSuccess)
{
	return FAppStyle::Get().GetBrush(bSuccess ? TEXT("Icons.SuccessWithColor") : TEXT("Icons.ErrorWithColor"));
}

void FN2CMcpNotification::Show(const FText& Text, bool bSuccess, float ExpireDuration)
{
	FNotificationInfo Info(Text);
	Info.ExpireDuration = ExpireDuration;
	Info.bFireAndForget = true;
	Info.Image = Icon(bSuccess);
	FSlateNotificationManager::Get().AddNotification(Info);
}
