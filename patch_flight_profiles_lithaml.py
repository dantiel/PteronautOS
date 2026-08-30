#!/usr/bin/env python3
"""Patch flight-profiles-panel.lithaml to use _t() calls"""

import re

# Read the file
with open('src/pages/flight-profiles-panel.lithaml', 'r', encoding='utf-8') as f:
    content = f.read()

# Define replacements
replacements = [
    # Line 4: Loading message
    ('Loading configuration — fields unlock once state arrives…', 
     '= self._t(\'ornithopter.loading_config\')'),
    
    # Line 17: Freq
    ('%b Freq', 
     '%b= self._t(\'ornithopter.flight_profiles.freq\')'),
    
    # Line 21: Profile
    ('%b Profile', 
     '%b= self._t(\'ornithopter.flight_profiles.profile\')'),
    
    # Line 38: Flight Profiles title (duplicate)
    ('.mui--text-title Flight Profiles', 
     '.mui--text-title= self._t(\'ornithopter.flight_profiles.title\')'),
    
    # Line 39: Per-profile tuning
    ('Per-profile tuning. Switch in flight via CH7 (3-pos).', 
     '= self._t(\'ornithopter.flight_profiles.per_profile_tuning\')'),
    
    # Line 43/45: Profile #{i}
    ('"Profile #{i}"', 
     'self._t(\'ornithopter.flight_profiles.profile_n\', {n: i})'),
    
    # Line 46: Active (CH7): Profile #{...}
    ('"Active (CH7): Profile #{self.activeFlightProfile}"', 
     'self._t(\'ornithopter.flight_profiles.active_ch7\', {n: self.activeFlightProfile})'),
    
    # Line 48: Editing warning
    ('⚠ Editing Profile #{self.editProfile} (not active) — changes save, but live output follows the active CH7 profile.', 
     '= self._t(\'ornithopter.flight_profiles.editing_inactive\', {n: self.editProfile})'),
    
    # Line 54: Glide Angle
    ('%b Glide Angle', 
     '%b= self._t(\'ornithopter.glide_angle.title\')'),
    
    # Line 55: glide desc
    ('Static wing angle (both wings)', 
     '= self._t(\'ornithopter.glide_angle.desc\')'),
    
    # Line 63: Flapping Angle
    ('%b Flapping Angle', 
     '%b= self._t(\'ornithopter.flapping_angle.title\')'),
    
    # Line 64: flapping desc
    ('Flap stroke centre offset (degrees)', 
     '= self._t(\'ornithopter.flapping_angle.desc\')'),
    
    # Line 71: Stroke Ferocity
    ('%b Stroke Ferocity', 
     '%b= self._t(\'ornithopter.stroke_ferocity.title\')'),
    
    # Line 72: stroke desc
    ('Downstroke aggression', 
     '= self._t(\'ornithopter.stroke_ferocity.desc\')'),
    
    # Line 80: Return Ferocity
    ('%b Return Ferocity', 
     '%b= self._t(\'ornithopter.return_ferocity.title\')'),
    
    # Line 81: return desc
    ('Upstroke speed', 
     '= self._t(\'ornithopter.return_ferocity.desc\')'),
    
    # Line 138: Channel Test title
    ('.mui--text-title Channel Test (CH1–CH7)', 
     '.mui--text-title= self._t(\'ornithopter.channel_test.title\')'),
    
    # Line 141: Override ON
    ("'Override ON — servos live'", 
     "self._t('ornithopter.channel_test.override_on')"),
    
    # Line 141: Override OFF
    ("'Override OFF'", 
     "self._t('ornithopter.channel_test.override_off')"),
    
    # Line 143: PWM hint
    ('PWM 1000–2000µs · centre 1500µs. Aileron/Elevator/Rudder spring to centre; Throttle/Arm/Freq/Profile hold.', 
     '= self._t(\'ornithopter.channel_test.hint\')'),
    
    # Line 152: spring
    ('spring', 
     '= self._t(\'ornithopter.channel_test.spring\')'),
    
    # Line 154: hold
    ('hold', 
     '= self._t(\'ornithopter.channel_test.hold\')'),
    
    # Line 158: Live Mixer Output
    ('Live Mixer Output', 
     '= self._t(\'ornithopter.mixer_output.title\')'),
    
    # Line 163: Override
    ('%b Override', 
     '%b= self._t(\'ornithopter.mixer_output.override\')'),
    
    # Line 175: Left Wing
    ('%b Left Wing', 
     '%b= self._t(\'ornithopter.mixer_output.left_wing\')'),
    
    # Line 181: Right Wing
    ('%b Right Wing', 
     '%b= self._t(\'ornithopter.mixer_output.right_wing\')'),
]

# Apply replacements
for old, new in replacements:
    if old in content:
        content = content.replace(old, new)
        print(f"✓ Replaced: {old[:40]}...")
    else:
        print(f"✗ NOT found: {old[:40]}...")

# Write back
with open('src/pages/flight-profiles-panel.lithaml', 'w', encoding='utf-8') as f:
    f.write(content)

print("\n✅ Patch applied!")
