#include <Geode/DefaultInclude.hpp>

#ifdef GEODE_IS_MACOS

#include <Geode/modify/AppDelegate.hpp>
#include <objc/message.h>
#import <AppKit/NSOpenGLView.h>

using namespace geode::prelude;

@interface EAGLView : NSOpenGLView
+(EAGLView*) sharedEGLView;

-(void) lockOpenGLContext;
-(void) unlockOpenGLContext;
@end

// older versions of macos apparently set the opengl context to the current thread upon creation
// this is a behavior that 1.9 relied on during very early init, for fetching information in CCConfiguration::gatherGPUInfo
// without it, launching will fail very amazingly with a null string construction exception
// locking the context as soon as possible loosely replicates the fix made in future versions of the game
// aka it's good enough!
struct $modify(AppDelegate) {
    bool applicationDidFinishLaunching() override {
        static Class eaglViewClass = objc_getClass("EAGLView");
        auto eaglView = [eaglViewClass sharedEGLView];

        [eaglView lockOpenGLContext];
        auto r = AppDelegate::applicationDidFinishLaunching();
        [eaglView unlockOpenGLContext];

        return r;
    }
};

#endif
