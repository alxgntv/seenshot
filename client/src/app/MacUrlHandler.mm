#include "app/MacUrlHandler.h"

#include <QDebug>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

namespace {

std::function<void(const QUrl &)> g_onUrl;

void handleGetUrl(const QUrl &url)
{
    qInfo() << "MacUrlHandler: open" << url.toString(QUrl::RemoveQuery);
    if (g_onUrl) {
        g_onUrl(url);
    }
}

} // namespace

@interface SeenShotUrlDelegate : NSObject
- (void)handleGetURLEvent:(NSAppleEventDescriptor *)event
           withReplyEvent:(NSAppleEventDescriptor *)reply;
@end

@implementation SeenShotUrlDelegate
- (void)handleGetURLEvent:(NSAppleEventDescriptor *)event
           withReplyEvent:(NSAppleEventDescriptor *)reply
{
    (void)reply;
    NSString *raw = [[event paramDescriptorForKeyword:keyDirectObject] stringValue];
    qInfo() << "MacUrlHandler: Apple Event chars=" << (int)raw.length;
    if (raw.length == 0) {
        return;
    }
    handleGetUrl(QUrl(QString::fromNSString(raw)));
}
@end

void MacUrlHandler::install(const std::function<void(const QUrl &)> &onUrl)
{
    g_onUrl = onUrl;
    static SeenShotUrlDelegate *delegate = nil;
    if (!delegate) {
        delegate = [[SeenShotUrlDelegate alloc] init];
    }
    [[NSAppleEventManager sharedAppleEventManager]
        setEventHandler:delegate
            andSelector:@selector(handleGetURLEvent:withReplyEvent:)
          forEventClass:kInternetEventClass
             andEventID:kAEGetURL];
    qInfo() << "MacUrlHandler: kAEGetURL installed";
}
