#!/usr/bin/env python3
"""Add all missing flight-profiles i18n keys to all locales"""

import re
import os

# Keys to add with translations
NEW_KEYS = {
    'ornithopter.flight_profiles.freq': {
        'en': 'Freq',
        'de': 'Freq',
        'es': 'Frec',
        'fr': 'Freq',
        'pt': 'Freq',
        'ru': 'Част',
        'ja': '周波数',
        'ko': '주파수',
        'zh': '频率',
    },
    'ornithopter.flight_profiles.profile': {
        'en': 'Profile',
        'de': 'Profil',
        'es': 'Perfil',
        'fr': 'Profil',
        'pt': 'Perfil',
        'ru': 'Профиль',
        'ja': 'プロファイル',
        'ko': '프로필',
        'zh': '配置',
    },
    'ornithopter.flight_profiles.per_profile_tuning': {
        'en': 'Per-profile tuning. Switch in flight via CH7 (3-pos).',
        'de': 'Profil-abhängige Einstellung. Umschalten im Flug über CH7 (3-Pos).',
        'es': 'Ajuste por perfil. Cambia en vuelo mediante CH7 (3 posiciones).',
        'fr': 'Réglage par profil. Changez en vol via CH7 (3 positions).',
        'pt': 'Ajuste por perfil. Troque em voo via CH7 (3 posições).',
        'ru': 'Настройка для каждого профиля. Переключение в полёте через CH7 (3 поз.).',
    },
    'ornithopter.flight_profiles.profile_n': {
        'en': 'Profile {n}',
        'de': 'Profil {n}',
        'es': 'Perfil {n}',
        'fr': 'Profil {n}',
        'pt': 'Perfil {n}',
        'ru': 'Профиль {n}',
        'ja': 'プロファイル {n}',
        'ko': '프로필 {n}',
        'zh': '配置 {n}',
    },
    'ornithopter.flight_profiles.active_ch7': {
        'en': 'Active (CH7): Profile {n}',
        'de': 'Aktiv (CH7): Profil {n}',
        'es': 'Activo (CH7): Perfil {n}',
        'fr': 'Actif (CH7): Profil {n}',
        'pt': 'Ativo (CH7): Perfil {n}',
        'ru': 'Активен (CH7): Профиль {n}',
    },
    'ornithopter.flight_profiles.editing_inactive': {
        'en': '⚠ Editing Profile {n} (not active) — changes save, but live output follows the active CH7 profile.',
        'de': '⚠ Bearbeite Profil {n} (nicht aktiv) — Änderungen werden gespeichert, aber die live-Ausgabe folgt dem aktiven CH7-Profil.',
        'es': '⚠ Editando Perfil {n} (no activo) — los cambios se guardan, pero la salida en vivo sigue el perfil CH7 activo.',
        'fr': '⚠ Édition du Profil {n} (non actif) — les modifications sont enregistrées, mais la sortie suit le profil CH7 actif.',
        'pt': '⚠ Editando Perfil {n} (não ativo) — alterações são salvas, mas a saída ao vivo segue o perfil CH7 ativo.',
        'ru': '⚠ Редактирование Профиля {n} (не активен) — изменения сохраняются, но вывод следует активному профилю CH7.',
    },
    'ornithopter.glide_angle.title': {
        'en': 'Glide Angle',
        'de': 'Gleitwinkel',
        'es': 'Ángulo de Planeo',
        'fr': 'Angle de Plané',
        'pt': 'Ângulo de Planeio',
        'ru': 'Угол Планирования',
        'ja': '滑空角度',
        'ko': '활공 각도',
        'zh': '滑翔角度',
    },
    'ornithopter.glide_angle.desc': {
        'en': 'Static wing angle (both wings)',
        'de': 'Statischer Flügelwinkel (beide Flügel)',
        'es': 'Ángulo alar estático (ambas alas)',
        'fr': 'Angle d\'aile statique (les deux ailes)',
        'pt': 'Ângulo de asa estático (ambas as asas)',
        'ru': 'Статический угол крыла (оба крыла)',
    },
    'ornithopter.flapping_angle.title': {
        'en': 'Flapping Angle',
        'de': 'Schlagwinkel',
        'es': 'Ángulo de Batido',
        'fr': 'Angle de Battement',
        'pt': 'Ângulo de Batida',
        'ru': 'Угол Взмаха',
        'ja': '羽ばたき角度',
        'ko': '플래핑 각도',
        'zh': '拍打角度',
    },
    'ornithopter.flapping_angle.desc': {
        'en': 'Flap stroke centre offset (degrees)',
        'de': 'Schlagzentrum-Versatz (Grad)',
        'es': 'Desplazamiento del centro de batido (grados)',
        'fr': 'Décalage du centre de battement (degrés)',
        'pt': 'Deslocamento do centro da batida (graus)',
        'ru': 'Смещение центра взмаха (градусы)',
    },
    'ornithopter.stroke_ferocity.title': {
        'en': 'Stroke Ferocity',
        'de': 'Schlagstärke',
        'es': 'Fiereza del Golpe',
        'fr': 'Férocité du Coup',
        'pt': 'Fereza do Golpe',
        'ru': 'Свирепость Удара',
        'ja': '打撃の激しさ',
        'ko': '스트로크 강도',
        'zh': '击打强度',
    },
    'ornithopter.stroke_ferocity.desc': {
        'en': 'Downstroke aggression',
        'de': 'Abwärts-Schlag Aggressivität',
        'es': 'Agresión del golpe descendente',
        'fr': 'Agressivité du coup descendant',
        'pt': 'Agressividade do golpe descendente',
        'ru': 'Агрессивность нисходящего удара',
    },
    'ornithopter.return_ferocity.title': {
        'en': 'Return Ferocity',
        'de': 'Rückkehrstärke',
        'es': 'Fiereza del Retorno',
        'fr': 'Férocité du Retour',
        'pt': 'Fereza do Retorno',
        'ru': 'Свирепость Возврата',
        'ja': '復帰の激しさ',
        'ko': '복귀 강도',
        'zh': '返回强度',
    },
    'ornithopter.return_ferocity.desc': {
        'en': 'Upstroke speed',
        'de': 'Aufwärts-Geschwindigkeit',
        'es': 'Velocidad del golpe ascendente',
        'fr': 'Vitesse du coup montant',
        'pt': 'Velocidade do golpe ascendente',
        'ru': 'Скорость восходящего удара',
    },
    'ornithopter.channel_test.title': {
        'en': 'Channel Test (CH1–CH7)',
        'de': 'Kanal-Test (CH1–CH7)',
        'es': 'Prueba de Canales (CH1–CH7)',
        'fr': 'Test des Canaux (CH1–CH7)',
        'pt': 'Teste de Canais (CH1–CH7)',
        'ru': 'Тест Каналов (CH1–CH7)',
    },
    'ornithopter.channel_test.override_on': {
        'en': 'Override ON — servos live',
        'de': 'Override AN — Servos live',
        'es': 'Override ON — servos en vivo',
        'fr': 'Override ON — servos en direct',
        'pt': 'Override ON — servos ao vivo',
        'ru': 'Override ON — серверы в реальном времени',
    },
    'ornithopter.channel_test.override_off': {
        'en': 'Override OFF',
        'de': 'Override AUS',
        'es': 'Override OFF',
        'fr': 'Override OFF',
        'pt': 'Override OFF',
        'ru': 'Override OFF',
    },
    'ornithopter.channel_test.hint': {
        'en': 'PWM 1000–2000µs · centre 1500µs. Aileron/Elevator/Rudder spring to centre; Throttle/Arm/Freq/Profile hold.',
        'de': 'PWM 1000–2000µs · Mitte 1500µs. Querruder/Höhenruder/Seitenruder federn zur Mitte; Throttle/Arm/Freq/Profile halten.',
        'es': 'PWM 1000–2000µs · centro 1500µs. Aileron/Elevador/Timón vuelven al centro; Throttle/Arm/Freq/Profile mantienen.',
        'fr': 'PWM 1000–2000µs · centre 1500µs. Aileron/Elevator/Rudder reviennent au centre; Throttle/Arm/Freq/Profile maintiennent.',
        'pt': 'PWM 1000–2000µs · centro 1500µs. Aileron/Elevador/Leme voltam ao centro; Throttle/Arm/Freq/Profile mantêm.',
        'ru': 'PWM 1000–2000µs · центр 1500µs. Элерон/Руль высоты/Руль направления возвращаются в центр; Throttle/Arm/Freq/Profile удерживаются.',
    },
    'ornithopter.channel_test.spring': {
        'en': 'spring',
        'de': 'Feder',
        'es': 'resorte',
        'fr': 'ressort',
        'pt': 'mola',
        'ru': 'пружина',
    },
    'ornithopter.channel_test.hold': {
        'en': 'hold',
        'de': 'halten',
        'es': 'mantener',
        'fr': 'maintenir',
        'pt': 'manter',
        'ru': 'удерживать',
    },
    'ornithopter.mixer_output.title': {
        'en': 'Live Mixer Output',
        'de': 'Live Mixer-Ausgabe',
        'es': 'Salida del Mixer en Vivo',
        'fr': 'Sortie du Mixer en Direct',
        'pt': 'Saída do Mixer ao Vivo',
        'ru': 'Вывод Микшера в Реальном Времени',
    },
    'ornithopter.mixer_output.override': {
        'en': 'Override',
        'de': 'Override',
        'es': 'Override',
        'fr': 'Override',
        'pt': 'Override',
        'ru': 'Override',
    },
    'ornithopter.mixer_output.left_wing': {
        'en': 'Left Wing',
        'de': 'Linker Flügel',
        'es': 'Ala Izquierda',
        'fr': 'Aile Gauche',
        'pt': 'Asa Esquerda',
        'ru': 'Левое Крыло',
        'ja': '左翼',
        'ko': '왼쪽 날개',
        'zh': '左翼',
    },
    'ornithopter.mixer_output.right_wing': {
        'en': 'Right Wing',
        'de': 'Rechter Flügel',
        'es': 'Ala Derecha',
        'fr': 'Aile Droite',
        'pt': 'Asa Direita',
        'ru': 'Правое Крыло',
        'ja': '右翼',
        'ko': '오른쪽 날개',
        'zh': '右翼',
    },
    'ornithopter.mixer_output.rudder': {
        'en': 'Rudder',
        'de': 'Seitenruder',
        'es': 'Timón',
        'fr': 'Gouvernail',
        'pt': 'Leme',
        'ru': 'Руль направления',
    },
    'ornithopter.saving': {
        'en': 'Saving...',
        'de': 'Speichern...',
        'es': 'Guardando...',
        'fr': 'Enregistrement...',
        'pt': 'Salvando...',
        'ru': 'Сохранение...',
    },
    'ornithopter.save_error': {
        'en': 'Save failed:',
        'de': 'Speichern fehlgeschlagen:',
        'es': 'Error al guardar:',
        'fr': 'Échec de l\'enregistrement:',
        'pt': 'Falha ao salvar:',
        'ru': 'Ошибка сохранения:',
    },
}

def get_lang_from_filename(filename):
    """Extract lang code from filename (en.js -> en)"""
    return os.path.basename(filename).replace('.js', '')

def add_keys_to_locale(filepath):
    """Add missing keys to locale file"""
    lang = get_lang_from_filename(filepath)
    
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Find the keys object - look for last key before closing
    lines = content.split('\n')
    
    # Find insertion point (before last closing brace that closes keys object)
    insert_idx = -1
    for i in range(len(lines) - 1, -1, -1):
        if lines[i].strip() == '};':
            insert_idx = i
            break
    
    if insert_idx == -1:
        print(f"ERROR: Could not find insertion point in {filepath}")
        return
    
    # Build new keys to insert
    new_lines = []
    for key, translations in NEW_KEYS.items():
        # Get translation for this lang, fallback to English
        value = translations.get(lang, translations.get('en', key))
        
        # Check if key already exists
        if f"'{key}'" in content or f'"{key}"' in content:
            continue
        
        new_lines.append(f"  '{key}': '{value}',")
    
    if not new_lines:
        print(f"  {lang}.js: All keys already present")
        return
    
    # Insert before closing brace
    for line in reversed(new_lines):
        lines.insert(insert_idx, line)
    
    # Write back
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    
    print(f"  {lang}.js: Added {len(new_lines)} keys")

# Process all locale files
locale_dir = 'src/locales'
for filename in sorted(os.listdir(locale_dir)):
    if filename.endswith('.js'):
        filepath = os.path.join(locale_dir, filename)
        add_keys_to_locale(filepath)
