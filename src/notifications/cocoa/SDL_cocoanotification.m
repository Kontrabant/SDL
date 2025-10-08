/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include "../SDL_notification_c.h"

#import <UserNotifications/UNUserNotificationCenter.h>
#import <UserNotifications/UNNotificationRequest.h>
#import <UserNotifications/UNNotificationContent.h>
#import <UserNotifications/UNNotificationSettings.h>
#import <UserNotifications/UNNotificationAction.h>
#import <UserNotifications/UNNotificationCategory.h>
#import <Foundation/Foundation.h>

/* Retrieving the notification center will crash with an unhandled
 * exception if attempted without an application bundle.
 *
 * Check the proxy bundle and disable notifications if nul to avoid
 * this crash.
 */
@interface LSApplicationProxy
@end

@interface LSBundleProxy: NSObject
+ (nonnull LSApplicationProxy *)bundleProxyForCurrentProcess;
@end

SDL_NotificationID SDL_SYS_ShowNotification(const SDL_NotificationData *notification_info)
{
    if (@available(macOS 10.14, *)) {
        if ([LSBundleProxy bundleProxyForCurrentProcess] == nil) {
            SDL_SetError("macOS notifications not supported outside an application bundle");
            return 0;
        }

        UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];

        __block BOOL authorized = YES;
        [center getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings * _Nonnull settings) {
            if (settings.authorizationStatus == UNAuthorizationStatusNotDetermined) {
                UNAuthorizationOptions options = UNAuthorizationOptionAlert + UNAuthorizationOptionSound;
                [center requestAuthorizationWithOptions:options
                                      completionHandler:^(BOOL granted, NSError *_Nullable error) {
                                        if (!granted) {
                                            authorized = NO;
                                        }
                                      }];
            } else if (settings.authorizationStatus != UNAuthorizationStatusAuthorized) {
                authorized = NO; // Not authorized
            }
        }];

        if (!authorized) {
            return 0;
        }

        NSMutableArray *actions = [NSMutableArray arrayWithCapacity:notification_info->num_actions];
        for (int i = 0; i < notification_info->num_actions; ++i) {
            UNNotificationAction *action = [UNNotificationAction actionWithIdentifier:[NSString stringWithUTF8String:notification_info->actions[i].button_id]
                                                                                title:[NSString stringWithUTF8String:notification_info->actions[i].button_label]
                                                                              options:UNNotificationActionOptionNone];

            actions[i] = action;
        }

        NSString *category_id = [[NSUUID new] UUIDString];
        UNNotificationCategory *category = [UNNotificationCategory categoryWithIdentifier:category_id
                                                                                  actions:actions intentIdentifiers:@[]
                                                                                  options:UNNotificationCategoryOptionNone];
        NSSet *categories = [NSSet setWithObject:category];
        [center setNotificationCategories:categories];

        UNMutableNotificationContent *content = [UNMutableNotificationContent new];
        content.title = [NSString stringWithUTF8String:notification_info->title];
        content.body = [NSString stringWithUTF8String:notification_info->message];
        content.categoryIdentifier = category_id;

        NSString *identifier = @"SDLLocalNotification";
        UNNotificationRequest *request = [UNNotificationRequest requestWithIdentifier:identifier
                                                                              content:content
                                                                              trigger:nil];

        [center addNotificationRequest:request
                 withCompletionHandler:^(NSError *_Nullable error) {
                   if (error != nil) {
                       NSLog(@"Something went wrong: %@", error);
                   }
                 }];
    }

    return 0;
}
