/*
* Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany.
* Copyright 1993 by David Dawes <dawes@xfree86.org>
* Copyright 2002 by SuSE Linux AG, Author: Egbert Eich
* Copyright 1994-2002 by The XFree86 Project, Inc.
* Copyright 2002 by Paul Elliott
* (Ported from xf86-input-mouse, above copyrights taken from there)
* Copyright 2008 by Chris Salch
*
* Permission to use, copy, modify, distribute, and sell this software
* and its documentation for any purpose is hereby granted without
* fee, provided that the above copyright notice appear in all copies
* and that both that copyright notice and this permission notice
* appear in supporting documentation, and that the name of the authors
* not be used in advertising or publicity pertaining to distribution of the
* software without specific, written prior permission.  The authors make no
* representations about the suitability of this software for any
* purpose.  It is provided "as is" without express or implied
* warranty.
*
* THE AUTHORS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
* INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
* NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
* CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
* OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
* NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
* CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*
*/

/* Mouse wheel emulation code. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "evdev.h"

#define MSE_MAXBUTTONS 32
#define WHEEL_NOT_CONFIGURED 0

/* Local Funciton Prototypes */
static BOOL EvdevWheelEmuHandleButtonMap(InputInfoPtr pInfo, WheelAxisPtr pAxis, char *axis_name);
static void EvdevWheelEmuInertia(InputInfoPtr pInfo, WheelAxisPtr axis, int value);

/* Filter mouse button events */
BOOL
EvdevWheelEmuFilterButton(InputInfoPtr pInfo, unsigned int button, int value)
{
    EvdevPtr pEvdev = (EvdevPtr)pInfo->private;

    /* Has wheel emulation been configured to be enabled? */
    if (!pEvdev->emulateWheel.enabled)
	return FALSE;

    /* Check for EmulateWheelButton */
    if (pEvdev->emulateWheel.button == button) {
	pEvdev->emulateWheel.button_state = value;

	return TRUE;
    }

    /* Don't care about this event */
    return FALSE;
}

/* Filter mouse wheel events */
BOOL
EvdevWheelEmuFilterMotion(InputInfoPtr pInfo, struct input_event *pEv)
{
    EvdevPtr pEvdev = (EvdevPtr)pInfo->private;
    WheelAxisPtr pAxis = NULL;
    int value = pEv->value;

    /* Has wheel emulation been configured to be enabled? */
    if (!pEvdev->emulateWheel.enabled)
	return FALSE;

    /* Handle our motion events if the emuWheel button is pressed*/
    if (pEvdev->emulateWheel.button_state) {
	/* We don't want to intercept real mouse wheel events */
	switch(pEv->code) {
	case REL_X:
	    pAxis = &(pEvdev->emulateWheel.X);
	    break;

	case REL_Y:
	    pAxis = &(pEvdev->emulateWheel.Y);
	    break;

	default:
	    break;
	}

	/* If we found REL_X or REL_Y, emulate a mouse wheel */
	if (pAxis)
	    EvdevWheelEmuInertia(pInfo, pAxis, value);

	/* Eat motion events while emulateWheel button pressed. */
	return TRUE;
    }

    return FALSE;
}

/* Simulate inertia for our emulated mouse wheel */
static void
EvdevWheelEmuInertia(InputInfoPtr pInfo, WheelAxisPtr axis, int value)
{
    EvdevPtr pEvdev = (EvdevPtr)pInfo->private;
    int button;
    int inertia;

    /* if this axis has not been configured, just eat the motion */
    if (!axis->up_button)
	return;

    axis->traveled_distance += value;

    if (axis->traveled_distance < 0) {
	button = axis->up_button;
	inertia = -pEvdev->emulateWheel.inertia;
    } else {
	button = axis->down_button;
	inertia = pEvdev->emulateWheel.inertia;
    }

    /* Produce button press events for wheel motion */
    while(abs(axis->traveled_distance) > pEvdev->emulateWheel.inertia) {

	axis->traveled_distance -= inertia;
	xf86PostButtonEvent(pInfo->dev, 0, button, 1, 0, 0);
	xf86PostButtonEvent(pInfo->dev, 0, button, 0, 0, 0);
    }
}

/* Handle button mapping here to avoid code duplication,
returns true if a button mapping was found. */
static BOOL
EvdevWheelEmuHandleButtonMap(InputInfoPtr pInfo, WheelAxisPtr pAxis, char* axis_name)
{
    EvdevPtr pEvdev = (EvdevPtr)pInfo->private;
    char *option_string;

    pAxis->up_button = WHEEL_NOT_CONFIGURED;

    /* Check to see if there is configuration for this axis */
    option_string = xf86SetStrOption(pInfo->options, axis_name, NULL);
    if (option_string) {
	int up_button = 0;
	int down_button = 0;
	char *msg = NULL;

	if ((sscanf(option_string, "%d %d", &up_button, &down_button) == 2) &&
	    ((up_button > 0) && (up_button <= MSE_MAXBUTTONS)) &&
	    ((down_button > 0) && (down_button <= MSE_MAXBUTTONS))) {

	    /* Use xstrdup to allocate a string for us */
	    msg = xstrdup("buttons XX and YY");

	    if (msg)
		sprintf(msg, "buttons %d and %d", up_button, down_button);

	    pAxis->up_button = up_button;
	    pAxis->down_button = down_button;

	    /* Update the number of buttons if needed */
	    if (up_button > pEvdev->buttons) pEvdev->buttons = up_button;
	    if (down_button > pEvdev->buttons) pEvdev->buttons = down_button;

	} else {
	    xf86Msg(X_WARNING, "%s: Invalid %s value:\"%s\"\n",
		    pInfo->name, axis_name, option_string);

	}

	/* Clean up and log what happened */
	if (msg) {
	    xf86Msg(X_CONFIG, "%s: %s: %s\n",pInfo->name, axis_name, msg);
	    xfree(msg);
	    return TRUE;
	}
    }
    return FALSE;
}

/* Setup the basic configuration options used by mouse wheel emulation */
void
EvdevWheelEmuPreInit(InputInfoPtr pInfo)
{
    EvdevPtr pEvdev = (EvdevPtr)pInfo->private;
    pEvdev->emulateWheel.enabled = FALSE;

    if (xf86SetBoolOption(pInfo->options, "EmulateWheel", FALSE)) {
	int wheelButton;
	int inertia;

	pEvdev->emulateWheel.enabled = TRUE;
	wheelButton = xf86SetIntOption(pInfo->options,
				       "EmulateWheelButton", 4);

	if ((wheelButton < 0) || (wheelButton > MSE_MAXBUTTONS)) {
	    xf86Msg(X_WARNING, "%s: Invalid EmulateWheelButton value: %d\n",
		    pInfo->name, wheelButton);
            xf86Msg(X_WARNING, "%s: Wheel emulation disabled.\n", pInfo->name);

	    pEvdev->emulateWheel.enabled = FALSE;
            return;
	}

	pEvdev->emulateWheel.button = wheelButton;

	inertia = xf86SetIntOption(pInfo->options, "EmulateWheelInertia", 10);

	if (inertia <= 0) {
	    xf86Msg(X_WARNING, "%s: Invalid EmulateWheelInertia value: %d\n",
		    pInfo->name, inertia);
            xf86Msg(X_WARNING, "%s: Using built-in inertia value.\n",
                    pInfo->name);

	    inertia = 10;
	}

	pEvdev->emulateWheel.inertia = inertia;

	/* Configure the Y axis or default it */
	if (!EvdevWheelEmuHandleButtonMap(pInfo, &(pEvdev->emulateWheel.Y),
					  "YAxisMapping")) {
	    /* Default the Y axis to sane values */
	    pEvdev->emulateWheel.Y.up_button = 4;
	    pEvdev->emulateWheel.Y.down_button = 5;

	    /* Simpler to check just the largest value in this case */
            /* XXX: we should post this to the server */
	    if (5 > pEvdev->buttons)
		pEvdev->buttons = 5;

	    /* Display default Configuration */
	    xf86Msg(X_CONFIG, "%s: YAxisMapping: buttons %d and %d\n",
		    pInfo->name, pEvdev->emulateWheel.Y.up_button,
		    pEvdev->emulateWheel.Y.down_button);
	}


	/* This axis should default to an unconfigured state as most people
	are not going to expect a Horizontal wheel. */
	EvdevWheelEmuHandleButtonMap(pInfo, &(pEvdev->emulateWheel.X),
				     "XAxisMapping");

	/* Used by the inertia code */
	pEvdev->emulateWheel.X.traveled_distance = 0;
	pEvdev->emulateWheel.Y.traveled_distance = 0;

	xf86Msg(X_CONFIG, "%s: EmulateWheelButton: %d, EmulateWheelInertia: %d\n",
		pInfo->name, pEvdev->emulateWheel.button, inertia);
    }
}

