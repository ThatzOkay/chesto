#if defined(SWITCH)
#include <switch.h>
#define PLATFORM "Switch"
#elif defined(__WIIU__)
#define PLATFORM "Wii U"
#include <coreinit/core.h>
#include <coreinit/foreground.h>
#include <proc_ui/procui.h>
#include <sysapp/launch.h>
#elif defined(_3DS)
#define PLATFORM "3DS"
#else
#define PLATFORM "Console"
#endif

#if defined(USE_RAMFS)
#include "../libs/resinfs/include/romfs-wiiu.h"
#endif

#include "RootDisplay.hpp"
#include "DownloadQueue.hpp"
#include "Button.hpp"

CST_Renderer* RootDisplay::renderer = NULL;
CST_Window* RootDisplay::window = NULL;
Element* RootDisplay::subscreen = NULL;
Element* RootDisplay::nextsubscreen = NULL;
RootDisplay* RootDisplay::mainDisplay = NULL;
bool RootDisplay::isDebug = false;

RootDisplay::RootDisplay()
{
	// initialize the romfs for switch/wiiu
#if defined(USE_RAMFS)
	ramfsInit();
#endif

	// initialize internal drawing library
	CST_DrawInit(this);

	this->x = 0;
	this->y = 0;
	this->width = SCREEN_WIDTH;
	this->height = SCREEN_HEIGHT;

	this->hasBackground = true;
#if defined(__WIIU__)
	this->backgroundColor = fromRGB(0x20, 154, 199);
#elif defined(_3DS) || defined(_3DS_MOCK)
	this->backgroundColor = fromRGB(0xe4, 0x00, 0x00);
#elif defined(SWITCH)
	this->backgroundColor = fromRGB(0xd6, 0x0 + 0x20, 0x12 + 0x20);
#else
	this->backgroundColor = fromRGB(0x42, 0x45, 0x48);
	this->backgroundColor = fromRGB(0x20, 154, 199);
	// this->backgroundColor = fromRGB(0xd6, 0x0 + 0x20, 0x12 + 0x20);
#endif

	// the main input handler
	this->events = new InputEvents();

	// initialize music (only if MUSIC defined)
	this->initMusic();
}

void RootDisplay::initMusic()
{
#ifdef SWITCH
	// no music if we're in applet mode
	// they use up too much memory, and a lot of people only use applet mode
	AppletType at = appletGetAppletType();
	if (at != AppletType_Application && at != AppletType_SystemApplication) {
		return;
	}
#endif

	// Initialize CST_mixer
	CST_MixerInit(this);
}

void RootDisplay::startMusic()
{
	CST_FadeInMusic(this);
}

RootDisplay::~RootDisplay()
{
	CST_DrawExit();

#if defined(USE_RAMFS)
	ramfsExit();
#endif
}

bool RootDisplay::process(InputEvents* event)
{
	if (nextsubscreen != subscreen)
	{
		delete subscreen;
		subscreen = nextsubscreen;
		return true;
	}

	if (RootDisplay::subscreen)
		return RootDisplay::subscreen->process(event);

	// keep processing child elements
	return super::process(event);
}

void RootDisplay::render(Element* parent)
{
	if (RootDisplay::subscreen)
	{
		RootDisplay::subscreen->render(this);
		this->update();
		return;
	}

	// render the rest of the subelements
	super::render(parent);

	// commit everything to the screen
	this->update();
}

void RootDisplay::update()
{
	// never exceed 60fps because there's no point
	// commented out, as if render isn't called manually,
	// the CST_Delay in the input processing loop should handle this

	//  int now = CST_GetTicks();
	//  int diff = now - this->lastFrameTime;

	//  if (diff < 16)
	//      return;

	CST_RenderPresent(this->renderer);
	//  this->lastFrameTime = now;
}

void RootDisplay::switchSubscreen(Element* next)
{
	if (nextsubscreen != subscreen)
		delete nextsubscreen;
	nextsubscreen = next;
}

#ifdef __WIIU__
// proc ui will block if it keeps control
void processWiiUHomeOverlay() {
		auto status = ProcUIProcessMessages(true);
    if (status == PROCUI_STATUS_EXITING)
			exit(0);
		else if (status == PROCUI_STATUS_RELEASE_FOREGROUND)
			ProcUIDrawDoneRelease();
}
#endif

int RootDisplay::mainLoop()
{
	// consoleDebugInit(debugDevice_SVC);
	// stdout = stderr; // for yuzu

#if defined(__WIIU__)
	// WHBLogUdpInit();
	// WHBLogCafeInit();
#endif

	DownloadQueue::init();

#ifdef __WIIU__
	// setup procui callback for resuming application to force a chesto render
	// https://stackoverflow.com/a/56145528 and http://bannalia.blogspot.com/2016/07/passing-capturing-c-lambda-functions-as.html
	auto updateDisplay = +[](void* display) -> unsigned int {
		((RootDisplay*)display)->futureRedrawCounter = 10;
		return 0;
	};
	ProcUIRegisterCallback(PROCUI_CALLBACK_ACQUIRE, updateDisplay, this, 100);
#endif

	while (isRunning)
	{
		bool atLeastOneNewEvent = false;
		bool viewChanged = false;

	#ifdef __WIIU__
		processWiiUHomeOverlay();
	#endif

		int frameStart = CST_GetTicks();

		// update download queue
		DownloadQueue::downloadQueue->process();

		// get any new input events
		while (events->update())
		{
			// process the inputs of the supplied event
			viewChanged |= this->process(events);
			atLeastOneNewEvent = true;

			// if we see a minus, exit immediately!
			if (events->pressed(SELECT_BUTTON) && this->canUseSelectToExit) {
				if (events->quitaction) events->quitaction();
				isRunning = false;
			}
		}

		// one more event update if nothing changed or there were no previous events seen
		// needed to non-input related processing that might update the screen to take place
		if ((!atLeastOneNewEvent && !viewChanged))
		{
			events->update();
			viewChanged |= this->process(events);
		}

		// draw the display if we processed an event or the view
		if (viewChanged)
			this->render(NULL);
		else
		{
			// delay for the remainder of the frame to keep up to 60fps
			// (we only do this if we didn't draw to not waste energy
			// if we did draw, then proceed immediately without waiting for smoother progress bars / scrolling)
			int delayTime = (CST_GetTicks() - frameStart);
			if (delayTime < 0)
				delayTime = 0;
			if (delayTime < 16)
				CST_Delay(16 - delayTime);
		}

		// free up any elements that are in the trash
		this->recycle();
	}

	DownloadQueue::quit();

	delete events;

	if (!isProtected) delete this;

	return 0;
}

void RootDisplay::recycle()
{
	for (auto e : trash)
		e->wipeAll(true);
	trash.clear();
}