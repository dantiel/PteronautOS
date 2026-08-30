#!/usr/bin/env python3
"""
Add missing ornithopter.kernel.* and ornithopter.mixing.* i18n keys to all locale files.
"""

import os
import re

# Missing keys with English values
MISSING_KEYS = {
    'ornithopter.kernel.model_name': 'Model Name',
    'ornithopter.kernel.model_name_desc': 'Custom name for this pterosaur model',
    'ornithopter.kernel.model_name_placeholder': 'e.g., Archaeopteryx 135',
    'ornithopter.kernel.servo_speed': 'Servo Speed',
    'ornithopter.kernel.servo_speed_desc': 'Maximum servo rotation speed (°/sec)',
    'ornithopter.kernel.base_freq': 'Base Flap Frequency',
    'ornithopter.kernel.base_freq_desc': 'Wing beat frequency at neutral stick (Hz)',
    'ornithopter.kernel.servo_min_us': 'Servo Min Pulse',
    'ornithopter.kernel.servo_pulse_desc': 'Pulse width limits for servo range (µs)',
    'ornithopter.kernel.servo_max_us': 'Servo Max Pulse',
    'ornithopter.kernel.trim_title': 'Servo Trim',
    'ornithopter.kernel.trim_desc': 'Center offset for each servo in microseconds',
    'ornithopter.mixing.kernel_hint': 'Kernel handles waveform generation; Mixer handles control → servo mapping',
}

# Translations for each language
TRANSLATIONS = {
    'de': {
        'ornithopter.kernel.model_name': 'Modellname',
        'ornithopter.kernel.model_name_desc': 'Benutzerdefinierter Name für dieses Pterosaurier-Modell',
        'ornithopter.kernel.model_name_placeholder': 'z.B., Archaeopteryx 135',
        'ornithopter.kernel.servo_speed': 'Servo-Geschwindigkeit',
        'ornithopter.kernel.servo_speed_desc': 'Maximale Servo-Drehgeschwindigkeit (°/sek)',
        'ornithopter.kernel.base_freq': 'Basis-Schlagfrequenz',
        'ornithopter.kernel.base_freq_desc': 'Flügelschlagfrequenz bei neutralem Stick (Hz)',
        'ornithopter.kernel.servo_min_us': 'Servo Min-Puls',
        'ornithopter.kernel.servo_pulse_desc': 'Pulsweiten-Grenzen für Servobereich (µs)',
        'ornithopter.kernel.servo_max_us': 'Servo Max-Puls',
        'ornithopter.kernel.trim_title': 'Servo-Trim',
        'ornithopter.kernel.trim_desc': 'Mitten-Offset für jedes Servo in Mikrosekunden',
        'ornithopter.mixing.kernel_hint': 'Kernel erzeugt Wellenformen; Mixer mappt Steuerung → Servos',
    },
    'es': {
        'ornithopter.kernel.model_name': 'Nombre del Modelo',
        'ornithopter.kernel.model_name_desc': 'Nombre personalizado para este modelo de pterosaurio',
        'ornithopter.kernel.model_name_placeholder': 'ej., Archaeopteryx 135',
        'ornithopter.kernel.servo_speed': 'Velocidad del Servo',
        'ornithopter.kernel.servo_speed_desc': 'Velocidad máxima de rotación del servo (°/seg)',
        'ornithopter.kernel.base_freq': 'Frecuencia Base de Aleteo',
        'ornithopter.kernel.base_freq_desc': 'Frecuencia de batido de alas en stick neutro (Hz)',
        'ornithopter.kernel.servo_min_us': 'Pulso Mínimo del Servo',
        'ornithopter.kernel.servo_pulse_desc': 'Límites de ancho de pulso para el rango del servo (µs)',
        'ornithopter.kernel.servo_max_us': 'Pulso Máximo del Servo',
        'ornithopter.kernel.trim_title': 'Ajuste del Servo',
        'ornithopter.kernel.trim_desc': 'Desplazamiento central para cada servo en microsegundos',
        'ornithopter.mixing.kernel_hint': 'El kernel genera formas de onda; el mixer mapea controles → servos',
    },
    'fr': {
        'ornithopter.kernel.model_name': 'Nom du Modèle',
        'ornithopter.kernel.model_name_desc': 'Nom personnalisé pour ce modèle de ptérosaure',
        'ornithopter.kernel.model_name_placeholder': 'ex., Archaeopteryx 135',
        'ornithopter.kernel.servo_speed': 'Vitesse du Servo',
        'ornithopter.kernel.servo_speed_desc': "Vitesse maximale de rotation du servo (°/sec)",
        'ornithopter.kernel.base_freq': 'Fréquence de Battement de Base',
        'ornithopter.kernel.base_freq_desc': 'Fréquence de battement des ailes au stick neutre (Hz)',
        'ornithopter.kernel.servo_min_us': 'Impulsion Min du Servo',
        'ornithopter.kernel.servo_pulse_desc': "Limites de largeur d'impulsion pour la plage du servo (µs)",
        'ornithopter.kernel.servo_max_us': 'Impulsion Max du Servo',
        'ornithopter.kernel.trim_title': 'Réglage du Servo',
        'ornithopter.kernel.trim_desc': 'Décalage central pour chaque servo en microsecondes',
        'ornithopter.mixing.kernel_hint': "Le noyau génère les formes d'onde; le mixer mappe contrôles → servos",
    },
    'pt': {
        'ornithopter.kernel.model_name': 'Nome do Modelo',
        'ornithopter.kernel.model_name_desc': 'Nome personalizado para este modelo de pterossauro',
        'ornithopter.kernel.model_name_placeholder': 'ex., Archaeopteryx 135',
        'ornithopter.kernel.servo_speed': 'Velocidade do Servo',
        'ornithopter.kernel.servo_speed_desc': 'Velocidade máxima de rotação do servo (°/seg)',
        'ornithopter.kernel.base_freq': 'Frequência Base de Batida',
        'ornithopter.kernel.base_freq_desc': 'Frequência de batida das asas no stick neutro (Hz)',
        'ornithopter.kernel.servo_min_us': 'Pulso Mínimo do Servo',
        'ornithopter.kernel.servo_pulse_desc': 'Limites de largura de pulso para o intervalo do servo (µs)',
        'ornithopter.kernel.servo_max_us': 'Pulso Máximo do Servo',
        'ornithopter.kernel.trim_title': 'Ajuste do Servo',
        'ornithopter.kernel.trim_desc': 'Deslocamento central para cada servo em microssegundos',
        'ornithopter.mixing.kernel_hint': 'O kernel gera formas de onda; o mixer mapeia controles → servos',
    },
}

LOCALE_DIR = 'src/locales'

def add_keys_to_locale(lang, translations=None):
    """Add missing keys to a locale file."""
    if translations is None:
        translations = MISSING_KEYS
    
    filepath = os.path.join(LOCALE_DIR, f'{lang}.js')
    if not os.path.exists(filepath):
        print(f"⚠️  File not found: {filepath}")
        return
    
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Check if keys already exist
    keys_to_add = {}
    for key, value in translations.items():
        if f"'{key}'" not in content and f'"{key}"' not in content:
            keys_to_add[key] = value
    
    if not keys_to_add:
        print(f"✓ {lang}.js: All keys already present")
        return
    
    # Find insertion point (after ornithopter.kernel.gearbox)
    lines = content.split('\n')
    insert_line = None
    
    for i, line in enumerate(lines):
        if "'ornithopter.kernel.gearbox'" in line or "'ornithopter.kernel.servo_drive'" in line:
            insert_line = i + 1
            break
    
    if insert_line is None:
        # Fallback: insert before ornithopter.mixing.title
        for i, line in enumerate(lines):
            if "'ornithopter.mixing.title'" in line:
                insert_line = i
                break
    
    if insert_line is None:
        # Last fallback: insert before closing brace
        for i in range(len(lines) - 1, -1, -1):
            if lines[i].strip() == '};':
                insert_line = i
                break
    
    if insert_line is None:
        print(f"⚠️  {lang}.js: Could not find insertion point")
        return
    
    # Build the new key lines
    indent = '    '
    new_lines = []
    for key, value in keys_to_add.items():
        value_escaped = value.replace("'", "\\'")
        new_lines.append(f"{indent}'{key}': '{value_escaped}',")
    
    # Insert the new lines
    for j, new_line in enumerate(new_lines):
        lines.insert(insert_line + j, new_line)
    
    # Write back
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    
    print(f"✓ {lang}.js: Added {len(new_lines)} keys")

def main():
    os.chdir('/Users/d/Desktop/HOI_KOSMOI/paraeksperiment/PteronautOS/src/html')
    
    # Add English first
    add_keys_to_locale('en', MISSING_KEYS)
    
    # Add translations to other languages
    for lang, trans in TRANSLATIONS.items():
        add_keys_to_locale(lang, trans)
    
    # Add English fallback to remaining languages
    remaining_langs = ['ru', 'ja', 'ko', 'zh', 'ar', 'hi']
    for lang in remaining_langs:
        add_keys_to_locale(lang, MISSING_KEYS)
    
    print("\n✅ Done!")

if __name__ == '__main__':
    main()
