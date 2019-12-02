///////////////////////////////////////////////////////////////////////////////
// JSHint configuration                                                      //
///////////////////////////////////////////////////////////////////////////////
/* global engine                                                             */
/* global print                                                              */
/* global midi                                                               */
/////////////////////////////////////////////////////////////////////////////// 
// ****************************************************************************
// * Mixxx mapping script file for the Hercules DJControl Jogvision.
// * Author: DJ Phatso, contributions by Kerrick Staley and David TV
// * Version 1.3 (November 2019)
// * Version 1.2 (November 2019)
// * Version 1.1 (March 2019)
// * Forum: https://www.mixxx.org/forums/viewtopic.php?f=7&t=12580
// * Wiki: https://www.mixxx.org/wiki/doku.php/hercules_dj_control_jogvision
//
// Changes to v1.3
// - Enabled the creation of beatloop (beatloop_activate) by using SHIFT+LoopON
// - Changed "LOOP SIZE" to adjust the beatjump_size (you still can change size
//   with surrounding buttons)
// - Added SHIFT+"LOOP SIZE" to move loop left or right by N beats (beatjump)
//
// Changes to v1.2
// - Enabled Jogwheel Outer LED rotation
// - Enabled Beat LEDs
// 
// Changes to v1.1
// - Controller knob/slider values are queried on startup, so MIXXX is synced.
// - Fixed vinyl button behavior the first time it's pressed.
// 
// v1.0 : Original release
//
// ****************************************************************************

var on = 0x7F;
var off = 0x00;
var alpha = 1.0 / 8;
var beta = alpha / 16;
var ledRotationSpeed = 4;
var ledRotationTimer;

var DJCJV = {};

DJCJV.scratchButtonState = true;
DJCJV.Channel=[];
DJCJV.Channel['[Channel1]'] = { "central":0x90, "deck":0xB0, "beatPosition":1, "rotation":0x00, "n":1 };
DJCJV.Channel['[Channel2]'] = { "central":0x91, "deck":0xB1, "beatPosition":1, "rotation":0x00, "n":2 };

// Initialization
DJCJV.init = function() {

	//Set all LED states to off
	midi.sendShortMsg(0xB0, 0x7F, off);
	midi.sendShortMsg(0xB1, 0x7F, off);

	// Set Vinyl button LED On.
	midi.sendShortMsg(0x90, 0x45, on);
	DJCJV.scratchButtonState = true;
	midi.sendShortMsg(0x90, 0x46, on);

	// Set Headphone CUE/MIX LED state
	if (engine.getValue("[Master]", "headMix") > 0.5) {
		midi.sendShortMsg(0x90, 0x4C, on); // headset "Mix" button LED
		midi.sendShortMsg(0x90, 0x4D, off);
	}
	else {
		midi.sendShortMsg(0x90, 0x4C, off);
		midi.sendShortMsg(0x90, 0x4D, on); // headset "Cue" button LED
	}

	//Enable Soft takeover
	engine.softTakeover("[Master]", "crossfader", true);
	engine.softTakeover("[QuickEffectRack1_[Channel1]]", "super1", true);
	engine.softTakeover("[QuickEffectRack1_[Channel2]]", "super1", true);

	//Set effects Levels - Dry/Wet - Filters
	engine.setParameter("[EffectRack1_EffectUnit1_Effect1]", "meta", 0.6);
	engine.setParameter("[EffectRack1_EffectUnit1_Effect2]", "meta", 0.6);
	engine.setParameter("[EffectRack1_EffectUnit1_Effect3]", "meta", 0.6);
	engine.setParameter("[EffectRack1_EffectUnit2_Effect1]", "meta", 0.6);
	engine.setParameter("[EffectRack1_EffectUnit2_Effect2]", "meta", 0.6);
	engine.setParameter("[EffectRack1_EffectUnit2_Effect3]", "meta", 0.6);
	engine.setParameter("[EffectRack1_EffectUnit1]", "mix", 0.6);
	engine.setParameter("[EffectRack1_EffectUnit2]", "mix", 0.6);
	engine.setParameter("[QuickEffectRack1_[Channel1]]", "super1", 0.5);
	engine.setParameter("[QuickEffectRack1_[Channel2]]", "super1", 0.5);

	// Connect the VUMeters and Jog Inner LED
	engine.connectControl("[Channel1]", "VuMeter", "DJCJV.vuMeterUpdate");
	engine.connectControl("[Channel2]", "VuMeter", "DJCJV.vuMeterUpdate");
	engine.connectControl("[Channel1]", "playposition", "DJCJV.wheelInnerUpdate");
	engine.connectControl("[Channel2]", "playposition", "DJCJV.wheelInnerUpdate");

	// Connect the the beat_active with beat leds
	engine.connectControl("[Channel1]", "beat_active", "DJCJV.beatActive");
	engine.connectControl("[Channel2]", "beat_active", "DJCJV.beatActive");
	engine.connectControl("[Channel1]", "stop", "DJCJV.beatInactive");
	engine.connectControl("[Channel2]", "stop", "DJCJV.beatInactive");
	
	// Set inner jog leds to 0
	midi.sendShortMsg(DJCJV.Channel['[Channel1]'].deck, 0x61, 0);
	midi.sendShortMsg(DJCJV.Channel['[Channel2]'].deck, 0x61, 0);
	// Set outer jog leds to 0
	midi.sendShortMsg(DJCJV.Channel['[Channel1]'].deck, 0x60, 1);
	midi.sendShortMsg(DJCJV.Channel['[Channel2]'].deck, 0x60, 1);
	
	// Enable wheels' outer leds rotation by timer (when channel is playing)
	ledRotationTimer = engine.beginTimer(20, function() {
		if (engine.getValue('[Channel1]', "play") == 1) {
			midi.sendShortMsg(DJCJV.Channel['[Channel1]'].deck, 0x60, DJCJV.Channel['[Channel1]'].rotation);
			DJCJV.Channel['[Channel1]'].rotation = DJCJV.Channel['[Channel1]'].rotation >= 127 ? 1 : DJCJV.Channel['[Channel1]'].rotation + ledRotationSpeed;
		}
		if (engine.getValue('[Channel2]', "play") == 1) {
			midi.sendShortMsg(DJCJV.Channel['[Channel2]'].deck, 0x60, DJCJV.Channel['[Channel2]'].rotation);
			DJCJV.Channel['[Channel2]'].rotation = DJCJV.Channel['[Channel2]'].rotation >= 127 ? 1 : DJCJV.Channel['[Channel2]'].rotation + ledRotationSpeed;
		}
	});

	// Ask the controller to send all current knob/slider values over MIDI, which will update the corresponding GUI controls in MIXXX.
	midi.sendShortMsg(0xB0, 0x7F, on);
	
    print("Hercules DJControl Jogvision initialized.");
};

// Finalization
DJCJV.shutdown = function() {
	if (DJCJV.ledRotationTimer) {
		engine.stopTimer(ledRotationTimer);
	}
	midi.sendShortMsg(0xB0, 0x7F, off);
	midi.sendShortMsg(0xB1, 0x7F, off);
	midi.sendShortMsg(0x90, 0x7F, off);
	midi.sendShortMsg(0x91, 0x7F, off);
};

// Beat led ACTIVATE (move)
DJCJV.beatActive = function(value, group, control) {
	if (value == 1) {
		return;
	}
	midi.sendShortMsg(DJCJV.Channel[group].central, 0x3A, DJCJV.Channel[group].beatPosition == 1 ? on : off);
	midi.sendShortMsg(DJCJV.Channel[group].central, 0x3B, DJCJV.Channel[group].beatPosition == 2 ? on : off);
	midi.sendShortMsg(DJCJV.Channel[group].central, 0x3C, DJCJV.Channel[group].beatPosition == 3 ? on : off);
	midi.sendShortMsg(DJCJV.Channel[group].central, 0x3D, DJCJV.Channel[group].beatPosition == 4 ? on : off);
	
	DJCJV.Channel[group].beatPosition  = DJCJV.Channel[group].beatPosition >= 4 ? 1 : DJCJV.Channel[group].beatPosition  + 1;
};
// Beat led DEACTIVATE (off all)
DJCJV.beatInactive = function(value, group, control) {
	midi.sendShortMsg(DJCJV.Channel[group].central, 0x3A, off);
	midi.sendShortMsg(DJCJV.Channel[group].central, 0x3B, off);
	midi.sendShortMsg(DJCJV.Channel[group].central, 0x3C, off);
	midi.sendShortMsg(DJCJV.Channel[group].central, 0x3D, off);
	
	DJCJV.Channel[group].beatPosition = 1;
};

//Jogwheels inner LED display - Play position
DJCJV.wheelInnerUpdate = function(value, group, control) {
	midi.sendShortMsg(DJCJV.Channel[group].deck, 0x61, value * 127);
};

//Vu Meter
DJCJV.vuMeterUpdate = function(value, group, control) {
	midi.sendShortMsg(DJCJV.Channel[group].deck, 0x44, value * 6);

};

// Headphone CUE/MIX buttons status
DJCJV.headCueOrMix = function(midino, control, value, status, group) {
	if (engine.getValue(group, "headMix") == 0) {
		engine.setValue(group, "headMix", -1.0);
		midi.sendShortMsg(0x90, 0x4C, off);
		midi.sendShortMsg(0x90, 0x4D, on);
	}
	else if (engine.getValue(group, "headMix") != 1) {
		engine.setValue(group, "headMix", 0);
		midi.sendShortMsg(0x90, 0x4C, on);
		midi.sendShortMsg(0x90, 0x4D, off);
	}
};

// Filter (Hercules' AIR FX)
DJCJV.Filter = function(channel, control, value, status, group) {
	engine.setValue(group, "super1", 0.5 - (value) / 255);
};

// Loop section
// SHIFT + Loop ON creates a loop at given point
DJCJV.beatloop_activate = function(channel, control, value, status, group) {
	engine.setValue(group, "beatloop_activate", value == 0 ? 0 : 1);
};
DJCJV.beatjump_move = function(channel, control, value, status, group) {
	if (value > 64) {
		engine.setValue(group, "beatjump_backward", 1);
		engine.setValue(group, "beatjump_backward", 0);
	}
	else {
		engine.setValue(group, "beatjump_forward", 1);
		engine.setValue(group, "beatjump_forward", 0);
	}
};
DJCJV.beatjump_size = function(channel, control, value, status, group) {
	var currentValue = engine.getValue(group, "beatjump_size");
	if (value > 64) {
		engine.setValue(group, "beatjump_size", currentValue /= 2);
	}
	else {
		engine.setValue(group, "beatjump_size", currentValue *= 2);
	}
};
DJCJV.loopsize = function(channel, control, value, status, group) {
	var currentValue = engine.getValue(group, "beatloop_size");
	if (value > 64) {
		engine.setValue(group, "beatloop_size", currentValue /= 2);
	}
	else {
		engine.setValue(group, "beatloop_size", currentValue *= 2);
	}
};

// The Vinyl button, used to enable or disable scratching on the jog wheels.
DJCJV.vinylButton = function(channel, control, value, status, group) {
	if (!value) {
		return;
	}

	if (DJCJV.scratchButtonState) {
		DJCJV.scratchButtonState = false;
		midi.sendShortMsg(0x90, 0x46, off);
	}
	else {
		DJCJV.scratchButtonState = true;
		midi.sendShortMsg(0x90, 0x46, on);
	}
};

// The pressure action over the jog wheel
DJCJV.wheelTouch = function(channel, control, value, status, group) {
	if (value > 0 && (engine.getValue(group, "play") != 1 || DJCJV.scratchButtonState)) {
		engine.scratchEnable(DJCJV.Channel[group].n, 400, 33 + 1 / 3, alpha, beta); //  Touching the wheel
	}
	else {
		engine.scratchDisable(DJCJV.Channel[group].n); // Released the wheel
	}
};

// Using the top of wheel for scratching (Vinyl button On) and bending (Vinyl button Off)
// In either case, register the movement
DJCJV.scratchWheel = function(channel, control, value, status, group) {
	if (engine.isScratching(DJCJV.Channel[group].n)) {
		engine.scratchTick(DJCJV.Channel[group].n, (value >= 64) ? value - 128 : value); // Scratch!
	}
	else {
		engine.setValue(group, 'jog', (value >= 64) ? value - 128 : value); // Pitch bend
	}
};

// Using the side of wheel for the bending
DJCJV.bendWheel = function(channel, control, value, status, group) {
	engine.setValue(group, 'jog', (value >= 64) ? value - 128 : value); // Pitch bend
};
