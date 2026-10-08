// Android entry point (NativeActivity + native_app_glue): Vulkan surface lifecycle, multi-touch, save on pause.
#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <time.h>

#include <memory>

#include "../core/fileio.h"
#include "../core/jobs.h"
#include "../core/log.h"
#include "../game/game.h"

using namespace gtabr;

namespace {
struct App {
  android_app* app = nullptr;
  std::unique_ptr<gfx::Renderer> renderer;
  std::unique_ptr<JobSystem> jobs;
  std::unique_ptr<Game> game;
  bool rendererReady = false;
  bool hasWindow = false;
  bool focused = false;
  bool gameStarted = false;
  double lastTime = 0;
  gfx::FrameData fd;
};

double nowSeconds() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// Immersive fullscreen (hide status + navigation bars) via JNI.
void hideSystemBars(android_app* app) {
  JNIEnv* env = nullptr;
  app->activity->vm->AttachCurrentThread(&env, nullptr);
  if (!env) return;
  jobject activity = app->activity->clazz;
  jclass actCls = env->GetObjectClass(activity);
  jmethodID getWindow = env->GetMethodID(actCls, "getWindow", "()Landroid/view/Window;");
  jobject window = env->CallObjectMethod(activity, getWindow);
  if (window) {
    jclass winCls = env->GetObjectClass(window);
    jmethodID getDecor = env->GetMethodID(winCls, "getDecorView", "()Landroid/view/View;");
    jobject decor = env->CallObjectMethod(window, getDecor);
    if (decor) {
      jclass viewCls = env->GetObjectClass(decor);
      jmethodID setVis = env->GetMethodID(viewCls, "setSystemUiVisibility", "(I)V");
      // IMMERSIVE_STICKY | FULLSCREEN | HIDE_NAVIGATION | LAYOUT_* flags
      const jint flags = 0x00001000 | 0x00000004 | 0x00000002 | 0x00000100 | 0x00000200 | 0x00000400;
      env->CallVoidMethod(decor, setVis, flags);
      env->DeleteLocalRef(viewCls);
      env->DeleteLocalRef(decor);
    }
    env->DeleteLocalRef(winCls);
    env->DeleteLocalRef(window);
  }
  env->DeleteLocalRef(actCls);
  if (env->ExceptionCheck()) env->ExceptionClear();
  app->activity->vm->DetachCurrentThread();
}

gfx::SurfaceFactory surfaceFactory(android_app* app) {
  return [app](VkInstance inst) {
    VkAndroidSurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    ci.window = app->window;
    VkSurfaceKHR s = VK_NULL_HANDLE;
    if (vkCreateAndroidSurfaceKHR(inst, &ci, nullptr, &s) != VK_SUCCESS) LOGE("vkCreateAndroidSurfaceKHR failed");
    return s;
  };
}

void onAppCmd(android_app* app, int32_t cmd) {
  App* a = (App*)app->userData;
  switch (cmd) {
    case APP_CMD_INIT_WINDOW: {
      if (!app->window) break;
      int w = ANativeWindow_getWidth(app->window), h = ANativeWindow_getHeight(app->window);
      if (!a->rendererReady) {
        a->renderer = std::make_unique<gfx::Renderer>();
        gfx::RendererConfig rc;
        rc.headless = false;
        rc.renderScale = 0.85f;
        rc.shadowMapSize = 2048;
        std::vector<const char*> exts = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
        if (!a->renderer->init(rc, surfaceFactory(app), exts)) {
          LOGE("Renderer init failed");
          ANativeActivity_finish(app->activity);
          break;
        }
        a->rendererReady = true;
      } else {
        a->renderer->resetSurface(surfaceFactory(app));
      }
      a->renderer->createSwapchain((uint32_t)w, (uint32_t)h);
      a->hasWindow = true;
      if (!a->gameStarted) {
        a->jobs = std::make_unique<JobSystem>(3);
        a->game = std::make_unique<Game>();
        Game::Init gi;
        gi.renderer = a->renderer.get();
        gi.jobs = a->jobs.get();
        gi.saveDir = app->activity->internalDataPath ? app->activity->internalDataPath : ".";
        gi.menu = true;   // the app opens on the main menu (CONTINUAR / NOVO JOGO / CARREGAR / CONFIGURAÇÕES / SAIR)
        a->game->init(gi);
        a->gameStarted = true;
      }
      a->game->setScreenSize((float)a->renderer->outputWidth(), (float)a->renderer->outputHeight());
      a->lastTime = nowSeconds();
      hideSystemBars(app);
      break;
    }
    case APP_CMD_TERM_WINDOW:
      a->hasWindow = false;
      if (a->game) a->game->onBackground();
      if (a->renderer) a->renderer->destroySwapchain();
      break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
      if (a->hasWindow && a->renderer && app->window) {
        a->renderer->createSwapchain((uint32_t)ANativeWindow_getWidth(app->window), (uint32_t)ANativeWindow_getHeight(app->window));
        if (a->game) a->game->setScreenSize((float)a->renderer->outputWidth(), (float)a->renderer->outputHeight());
      }
      break;
    case APP_CMD_GAINED_FOCUS:
      a->focused = true;
      a->lastTime = nowSeconds();
      hideSystemBars(app);
      break;
    case APP_CMD_LOST_FOCUS:
      a->focused = false;
      if (a->game) a->game->onBackground();
      break;
    case APP_CMD_PAUSE:
    case APP_CMD_SAVE_STATE:
    case APP_CMD_STOP:
      if (a->game) a->game->onBackground();
      break;
    default:
      break;
  }
}

int32_t onInput(android_app* app, AInputEvent* ev) {
  App* a = (App*)app->userData;
  if (!a->game || AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION) return 0;
  int32_t action = AMotionEvent_getAction(ev);
  int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
  size_t idx = (size_t)((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
  size_t count = AMotionEvent_getPointerCount(ev);
  auto send = [&](size_t i, TouchAction t) {
    a->game->onTouch((int)AMotionEvent_getPointerId(ev, i), t, AMotionEvent_getX(ev, i), AMotionEvent_getY(ev, i));
  };
  switch (masked) {
    case AMOTION_EVENT_ACTION_DOWN: send(0, TouchAction::Down); break;
    case AMOTION_EVENT_ACTION_POINTER_DOWN: send(idx, TouchAction::Down); break;
    case AMOTION_EVENT_ACTION_MOVE: for (size_t i = 0; i < count; ++i) send(i, TouchAction::Move); break;
    case AMOTION_EVENT_ACTION_UP: send(0, TouchAction::Up); break;
    case AMOTION_EVENT_ACTION_POINTER_UP: send(idx, TouchAction::Up); break;
    case AMOTION_EVENT_ACTION_CANCEL: for (size_t i = 0; i < count; ++i) send(i, TouchAction::Cancel); break;
    default: break;
  }
  return 1;
}
}  // namespace

void android_main(android_app* app) {
  App a;
  a.app = app;
  app->userData = &a;
  app->onAppCmd = onAppCmd;
  app->onInputEvent = onInput;
  fileio::setAndroidAssetManager(app->activity->assetManager);
  fileio::setSaveDir(app->activity->internalDataPath ? app->activity->internalDataPath : ".");

  while (true) {
    int events;
    android_poll_source* source = nullptr;
    bool animating = a.hasWindow && a.rendererReady && a.game;
    int timeout = animating ? 0 : -1;
    int r;
    while ((r = ALooper_pollOnce(timeout, nullptr, &events, (void**)&source)) >= 0) {
      if (source) source->process(app, source);
      if (app->destroyRequested) {
        if (a.game) { a.game->shutdown(); a.game.reset(); }
        a.jobs.reset();
        if (a.renderer) a.renderer->shutdown();
        return;
      }
      timeout = (a.hasWindow && a.rendererReady && a.game) ? 0 : -1;
    }
    if (a.hasWindow && a.rendererReady && a.game) {
      double t = nowSeconds();
      float dt = (float)(t - a.lastTime);
      a.lastTime = t;
      a.game->frame(dt, a.fd);
      a.renderer->renderFrame(a.fd);
      if (a.game->wantsQuit()) {
        a.game->onBackground();
        ANativeActivity_finish(app->activity);
      }
    }
  }
}
