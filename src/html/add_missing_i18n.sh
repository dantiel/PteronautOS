#!/bin/bash
set -e

# Function to add keys to a locale file
add_keys() {
    local lang=$1
    local file="src/locales/${lang}.js"
    
    # Check if file exists
    if [ ! -f "$file" ]; then
        echo "  $file not found"
        return
    fi
    
    # Count existing keys
    local existing=$(grep -c "app\.menu\.flight_profiles" "$file" || echo "0")
    
    if [ "$existing" -gt 0 ]; then
        echo "  $lang.js: keys already present"
        return
    fi
    
    # Find insertion point (before the closing }})
    local line_num=$(grep -n "^})" "$file" | head -1 | cut -d: -f1)
    
    if [ -z "$line_num" ]; then
        echo "  $lang.js: cannot find insertion point"
        return
    fi
    
    # Create temp file with new keys
    local temp_file=$(mktemp)
    head -n $((line_num - 1)) "$file" > "$temp_file"
    
    # Add keys based on language
    case $lang in
        de)
            cat >> "$temp_file" << 'EOF'
    'app.menu.flight_profiles': 'Flugprofile',
    'app.menu.link': 'Link',
    'app.menu.system': 'System',
    'app.menu.backup': 'Backup',
    'app.menu.diagnostics': 'Diagnose',
    'elrs.info.section_pteronautos': 'PteronautOS',
    'elrs.info.model_name': 'Modellname',
    'elrs.info.ornithopter_mode': 'Ornithopter Modus',
    'elrs.info.active': 'Aktiv',
    'elrs.info.kernel_type': 'Kernel-Typ',
    'elrs.info.kernel_servo': 'Kernel-Servo',
    'elrs.info.mixer_profile': 'Mixer-Profil',
    'elrs.info.zephyrus': 'Zephyrus Gyro',
    'elrs.info.link': 'Link',
    'elrs.info.section_device': 'Gerät',
    'ornithopter.kernel.model_name': 'Modellname',
    'ornithopter.kernel.model_name_desc': 'Benutzerdefinierter Name für dieses Pterosaurier-Modell',
    'ornithopter.kernel.model_name_placeholder': 'z.B. Archaeopteryx 135',
    'ornithopter.kernel.servo_speed': 'Servo-Geschwindigkeit',
    'ornithopter.kernel.servo_speed_desc': 'Maximale Servo-Drehgeschwindigkeit (°/s)',
    'ornithopter.kernel.base_freq': 'Basis-Schlagfrequenz',
    'ornithopter.kernel.base_freq_desc': 'Schlagfrequenz bei neutralem Stick (Hz)',
    'ornithopter.kernel.servo_min_us': 'Servo Min Impuls',
    'ornithopter.kernel.servo_pulse_desc': 'Impulsbreitengrenzen für den Servobereich (µs)',
    'ornithopter.kernel.servo_max_us': 'Servo Max Impuls',
    'ornithopter.kernel.trim_title': 'Servo-Trim',
    'ornithopter.kernel.trim_desc': 'Mittenversatz für jedes Servo in Mikrosekunden',
    'ornithopter.servo.left_wing': 'Linker Flügel',
    'ornithopter.servo.right_wing': 'Rechter Flügel',
    'ornithopter.servo.rudder': 'Seitenruder',
    'ornithopter.mixing.kernel_hint': 'Kernel erzeugt Wellenform; Mixer steuert Servo-Zuordnung',
    'ornithopter.loading_config': 'Lade Konfiguration...',
    'ornithopter.flight_profiles.per_profile_tuning': 'Profilspezifische Einstellungen',
    'ornithopter.flight_profiles.profile_n': 'Profil {n}',
    'ornithopter.flight_profiles.active_ch7': 'Aktiv via CH7',
    'ornithopter.flight_profiles.freq': 'Frequenz',
    'ornithopter.flight_profiles.profile': 'Profil',
    'ornithopter.glide_angle.title': 'Gleitwinkel',
    'ornithopter.glide_angle.desc': 'Gleitwinkel-Mittenoffset (-15..+15°)',
    'ornithopter.flapping_angle.title': 'Schlagwinkel',
    'ornithopter.flapping_angle.desc': 'Schlagwinkel-Mittenoffset (-15..+15°)',
    'ornithopter.stroke_ferocity.title': 'Schlag-Wucht',
    'ornithopter.stroke_ferocity.desc': 'Steuerung der Schlagintensität (0-100%)',
    'ornithopter.return_ferocity.title': 'Rückzug-Wucht',
    'ornithopter.return_ferocity.desc': 'Steuerung der Rückzugintensität (0-100%)',
    'ornithopter.channel_test.title': 'Kanal-Test',
    'ornithopter.channel_test.override_off': 'Override Aus',
    'ornithopter.channel_test.hint': 'Verwende Override um Kanäle manuell zu steuern',
    'ornithopter.channel_test.spring': 'Feder',
    'ornithopter.channel_test.hold': 'Halten',
    'ornithopter.mixer_output.title': 'Mixer-Ausgang',
    'ornithopter.mixer_output.override': 'Override',
    'ornithopter.mixer_output.left_wing': 'Linker Flügel',
    'ornithopter.mixer_output.right_wing': 'Rechter Flügel',
EOF
            ;;
        es)
            cat >> "$temp_file" << 'EOF'
    'app.menu.flight_profiles': 'Perfiles de Vuelo',
    'app.menu.link': 'Enlace',
    'app.menu.system': 'Sistema',
    'app.menu.backup': 'Respaldo',
    'app.menu.diagnostics': 'Diagnóstico',
    'elrs.info.section_pteronautos': 'PteronautOS',
    'elrs.info.model_name': 'Nombre del Modelo',
    'elrs.info.ornithopter_mode': 'Modo Ornithopter',
    'elrs.info.active': 'Activo',
    'elrs.info.kernel_type': 'Tipo de Kernel',
    'elrs.info.kernel_servo': 'Kernel-Servo',
    'elrs.info.mixer_profile': 'Perfil del Mixer',
    'elrs.info.zephyrus': 'Giroscopio Zephyrus',
    'elrs.info.link': 'Enlace',
    'elrs.info.section_device': 'Dispositivo',
    'ornithopter.kernel.model_name': 'Nombre del Modelo',
    'ornithopter.kernel.model_name_desc': 'Nombre personalizado para este modelo de pterosaurio',
    'ornithopter.kernel.model_name_placeholder': 'ej. Archaeopteryx 135',
    'ornithopter.kernel.servo_speed': 'Velocidad del Servo',
    'ornithopter.kernel.servo_speed_desc': 'Velocidad máxima de rotación del servo (°/s)',
    'ornithopter.kernel.base_freq': 'Frecuencia Base de Aleteo',
    'ornithopter.kernel.base_freq_desc': 'Frecuencia de aleteo en stick neutral (Hz)',
    'ornithopter.kernel.servo_min_us': 'Pulso Mínimo del Servo',
    'ornithopter.kernel.servo_pulse_desc': 'Límites de ancho de pulso para el rango del servo (µs)',
    'ornithopter.kernel.servo_max_us': 'Pulso Máximo del Servo',
    'ornithopter.kernel.trim_title': 'Ajuste del Servo',
    'ornithopter.kernel.trim_desc': 'Desplazamiento central para cada servo en microsegundos',
    'ornithopter.servo.left_wing': 'Ala Izquierda',
    'ornithopter.servo.right_wing': 'Ala Derecha',
    'ornithopter.servo.rudder': 'Timón',
    'ornithopter.mixing.kernel_hint': 'Kernel genera forma de onda; Mixer controla mapeo de servos',
    'ornithopter.loading_config': 'Cargando configuración...',
    'ornithopter.flight_profiles.per_profile_tuning': 'Ajustes por Perfil',
    'ornithopter.flight_profiles.profile_n': 'Perfil {n}',
    'ornithopter.flight_profiles.active_ch7': 'Activo via CH7',
    'ornithopter.flight_profiles.freq': 'Frecuencia',
    'ornithopter.flight_profiles.profile': 'Perfil',
    'ornithopter.glide_angle.title': 'Ángulo de Planeo',
    'ornithopter.glide_angle.desc': 'Desplazamiento central del ángulo de planeo (-15..+15°)',
    'ornithopter.flapping_angle.title': 'Ángulo de Aleteo',
    'ornithopter.flapping_angle.desc': 'Desplazamiento central del ángulo de aleteo (-15..+15°)',
    'ornithopter.stroke_ferocity.title': 'Fiereza del Golpe',
    'ornithopter.stroke_ferocity.desc': 'Control de la intensidad del golpe (0-100%)',
    'ornithopter.return_ferocity.title': 'Fiereza del Retorno',
    'ornithopter.return_ferocity.desc': 'Control de la intensidad del retorno (0-100%)',
    'ornithopter.channel_test.title': 'Prueba de Canales',
    'ornithopter.channel_test.override_off': 'Override Desactivado',
    'ornithopter.channel_test.hint': 'Usa override para controlar canales manualmente',
    'ornithopter.channel_test.spring': 'Resorte',
    'ornithopter.channel_test.hold': 'Mantener',
    'ornithopter.mixer_output.title': 'Salida del Mixer',
    'ornithopter.mixer_output.override': 'Override',
    'ornithopter.mixer_output.left_wing': 'Ala Izquierda',
    'ornithopter.mixer_output.right_wing': 'Ala Derecha',
EOF
            ;;
        fr)
            cat >> "$temp_file" << 'EOF'
    'app.menu.flight_profiles': 'Profils de Vol',
    'app.menu.link': 'Liaison',
    'app.menu.system': 'Système',
    'app.menu.backup': 'Sauvegarde',
    'app.menu.diagnostics': 'Diagnostic',
    'elrs.info.section_pteronautos': 'PteronautOS',
    'elrs.info.model_name': 'Nom du Modèle',
    'elrs.info.ornithopter_mode': 'Mode Ornithoptère',
    'elrs.info.active': 'Actif',
    'elrs.info.kernel_type': 'Type de Noyau',
    'elrs.info.kernel_servo': 'Noyau-Servo',
    'elrs.info.mixer_profile': 'Profil du Mixer',
    'elrs.info.zephyrus': 'Gyro Zephyrus',
    'elrs.info.link': 'Liaison',
    'elrs.info.section_device': 'Appareil',
    'ornithopter.kernel.model_name': 'Nom du Modèle',
    'ornithopter.kernel.model_name_desc': 'Nom personnalisé pour ce modèle de ptérosaure',
    'ornithopter.kernel.model_name_placeholder': 'ex. Archaeopteryx 135',
    'ornithopter.kernel.servo_speed': 'Vitesse du Servo',
    'ornithopter.kernel.servo_speed_desc': 'Vitesse maximale de rotation du servo (°/s)',
    'ornithopter.kernel.base_freq': 'Fréquence de Base de Battement',
    'ornithopter.kernel.base_freq_desc': 'Fréquence de battement au stick neutre (Hz)',
    'ornithopter.kernel.servo_min_us': 'Impulsion Min du Servo',
    'ornithopter.kernel.servo_pulse_desc': 'Limites de largeur dimpulsion pour la plage du servo (µs)',
    'ornithopter.kernel.servo_max_us': 'Impulsion Max du Servo',
    'ornithopter.kernel.trim_title': 'Réglage du Servo',
    'ornithopter.kernel.trim_desc': 'Décalage central pour chaque servo en microsecondes',
    'ornithopter.servo.left_wing': 'Aile Gauche',
    'ornithopter.servo.right_wing': 'Aile Droite',
    'ornithopter.servo.rudder': 'Gouvernail',
    'ornithopter.mixing.kernel_hint': 'Le noyau génère la forme donde; le mixer contrôle le mappage des servos',
    'ornithopter.loading_config': 'Chargement de la configuration...',
    'ornithopter.flight_profiles.per_profile_tuning': 'Réglages par Profil',
    'ornithopter.flight_profiles.profile_n': 'Profil {n}',
    'ornithopter.flight_profiles.active_ch7': 'Actif via CH7',
    'ornithopter.flight_profiles.freq': 'Fréquence',
    'ornithopter.flight_profiles.profile': 'Profil',
    'ornithopter.glide_angle.title': 'Angle de Plané',
    'ornithopter.glide_angle.desc': 'Décalage central de langle de plané (-15..+15°)',
    'ornithopter.flapping_angle.title': 'Angle de Battement',
    'ornithopter.flapping_angle.desc': 'Décalage central de langle de battement (-15..+15°)',
    'ornithopter.stroke_ferocity.title': 'Férocité du Coup',
    'ornithopter.stroke_ferocity.desc': 'Contrôle de lintensité du coup (0-100%)',
    'ornithopter.return_ferocity.title': 'Férocité du Retour',
    'ornithopter.return_ferocity.desc': 'Contrôle de lintensité du retour (0-100%)',
    'ornithopter.channel_test.title': 'Test de Canaux',
    'ornithopter.channel_test.override_off': 'Override Désactivé',
    'ornithopter.channel_test.hint': 'Utilisez loverride pour contrôler les canaux manuellement',
    'ornithopter.channel_test.spring': 'Ressort',
    'ornithopter.channel_test.hold': 'Maintenir',
    'ornithopter.mixer_output.title': 'Sortie du Mixer',
    'ornithopter.mixer_output.override': 'Override',
    'ornithopter.mixer_output.left_wing': 'Aile Gauche',
    'ornithopter.mixer_output.right_wing': 'Aile Droite',
EOF
            ;;
        pt)
            cat >> "$temp_file" << 'EOF'
    'app.menu.flight_profiles': 'Perfis de Voo',
    'app.menu.link': 'Link',
    'app.menu.system': 'Sistema',
    'app.menu.backup': 'Backup',
    'app.menu.diagnostics': 'Diagnóstico',
    'elrs.info.section_pteronautos': 'PteronautOS',
    'elrs.info.model_name': 'Nome do Modelo',
    'elrs.info.ornithopter_mode': 'Modo Ornithopter',
    'elrs.info.active': 'Ativo',
    'elrs.info.kernel_type': 'Tipo de Kernel',
    'elrs.info.kernel_servo': 'Kernel-Servo',
    'elrs.info.mixer_profile': 'Perfil do Mixer',
    'elrs.info.zephyrus': 'Giroscópio Zephyrus',
    'elrs.info.link': 'Link',
    'elrs.info.section_device': 'Dispositivo',
    'ornithopter.kernel.model_name': 'Nome do Modelo',
    'ornithopter.kernel.model_name_desc': 'Nome personalizado para este modelo de pterossauro',
    'ornithopter.kernel.model_name_placeholder': 'ex. Archaeopteryx 135',
    'ornithopter.kernel.servo_speed': 'Velocidade do Servo',
    'ornithopter.kernel.servo_speed_desc': 'Velocidade máxima de rotação do servo (°/s)',
    'ornithopter.kernel.base_freq': 'Frequência Base de Batida',
    'ornithopter.kernel.base_freq_desc': 'Frequência de batida no stick neutro (Hz)',
    'ornithopter.kernel.servo_min_us': 'Pulso Mínimo do Servo',
    'ornithopter.kernel.servo_pulse_desc': 'Limites de largura de pulso para o intervalo do servo (µs)',
    'ornithopter.kernel.servo_max_us': 'Pulso Máximo do Servo',
    'ornithopter.kernel.trim_title': 'Ajuste do Servo',
    'ornithopter.kernel.trim_desc': 'Deslocamento central para cada servo em microssegundos',
    'ornithopter.servo.left_wing': 'Asa Esquerda',
    'ornithopter.servo.right_wing': 'Asa Direita',
    'ornithopter.servo.rudder': 'Leme',
    'ornithopter.mixing.kernel_hint': 'Kernel gera forma de onda; Mixer controla mapeamento de servos',
    'ornithopter.loading_config': 'Carregando configuração...',
    'ornithopter.flight_profiles.per_profile_tuning': 'Ajustes por Perfil',
    'ornithopter.flight_profiles.profile_n': 'Perfil {n}',
    'ornithopter.flight_profiles.active_ch7': 'Ativo via CH7',
    'ornithopter.flight_profiles.freq': 'Frequência',
    'ornithopter.flight_profiles.profile': 'Perfil',
    'ornithopter.glide_angle.title': 'Ângulo de Planeio',
    'ornithopter.glide_angle.desc': 'Deslocamento central do ângulo de planeio (-15..+15°)',
    'ornithopter.flapping_angle.title': 'Ângulo de Batida',
    'ornithopter.flapping_angle.desc': 'Deslocamento central do ângulo de batida (-15..+15°)',
    'ornithopter.stroke_ferocity.title': 'Ferocidade do Golpe',
    'ornithopter.stroke_ferocity.desc': 'Controle da intensidade do golpe (0-100%)',
    'ornithopter.return_ferocity.title': 'Ferocidade do Retorno',
    'ornithopter.return_ferocity.desc': 'Controle da intensidade do retorno (0-100%)',
    'ornithopter.channel_test.title': 'Teste de Canais',
    'ornithopter.channel_test.override_off': 'Override Desativado',
    'ornithopter.channel_test.hint': 'Use override para controlar canais manualmente',
    'ornithopter.channel_test.spring': 'Mola',
    'ornithopter.channel_test.hold': 'Manter',
    'ornithopter.mixer_output.title': 'Saída do Mixer',
    'ornithopter.mixer_output.override': 'Override',
    'ornithopter.mixer_output.left_wing': 'Asa Esquerda',
    'ornithopter.mixer_output.right_wing': 'Asa Direita',
EOF
            ;;
        ru)
            cat >> "$temp_file" << 'EOF'
    'app.menu.flight_profiles': 'Профили Полёта',
    'app.menu.link': 'Связь',
    'app.menu.system': 'Система',
    'app.menu.backup': 'Резерв',
    'app.menu.diagnostics': 'Диагностика',
    'elrs.info.section_pteronautos': 'PteronautOS',
    'elrs.info.model_name': 'Название Модели',
    'elrs.info.ornithopter_mode': 'Режим Орнитоптера',
    'elrs.info.active': 'Активен',
    'elrs.info.kernel_type': 'Тип Ядра',
    'elrs.info.kernel_servo': 'Серво Ядра',
    'elrs.info.mixer_profile': 'Профиль Микшера',
    'elrs.info.zephyrus': 'Гироскоп Зефир',
    'elrs.info.link': 'Связь',
    'elrs.info.section_device': 'Устройство',
    'ornithopter.kernel.model_name': 'Название Модели',
    'ornithopter.kernel.model_name_desc': 'Пользовательское название этой модели птерозавра',
    'ornithopter.kernel.model_name_placeholder': 'напр. Archaeopteryx 135',
    'ornithopter.kernel.servo_speed': 'Скорость Серво',
    'ornithopter.kernel.servo_speed_desc': 'Максимальная скорость вращения сервопривода (°/s)',
    'ornithopter.kernel.base_freq': 'Базовая Частота Взмаха',
    'ornithopter.kernel.base_freq_desc': 'Частота взмаха при нейтральном стике (Гц)',
    'ornithopter.kernel.servo_min_us': 'Мин Импульс Серво',
    'ornithopter.kernel.servo_pulse_desc': 'Пределы ширины импульса для диапазона сервопривода (мкс)',
    'ornithopter.kernel.servo_max_us': 'Макс Импульс Серво',
    'ornithopter.kernel.trim_title': 'Трим Серво',
    'ornithopter.kernel.trim_desc': 'Центральное смещение для каждого сервопривода в микросекундах',
    'ornithopter.servo.left_wing': 'Левое Крыло',
    'ornithopter.servo.right_wing': 'Правое Крыло',
    'ornithopter.servo.rudder': 'Руль',
    'ornithopter.mixing.kernel_hint': 'Ядро генерирует форму волны; Микшер управляет отображением сервоприводов',
    'ornithopter.loading_config': 'Загрузка конфигурации...',
    'ornithopter.flight_profiles.per_profile_tuning': 'Настройки для Профиля',
    'ornithopter.flight_profiles.profile_n': 'Профиль {n}',
    'ornithopter.flight_profiles.active_ch7': 'Активен через CH7',
    'ornithopter.flight_profiles.freq': 'Частота',
    'ornithopter.flight_profiles.profile': 'Профиль',
    'ornithopter.glide_angle.title': 'Угол Планирования',
    'ornithopter.glide_angle.desc': 'Центральное смещение угла планирования (-15..+15°)',
    'ornithopter.flapping_angle.title': 'Угол Взмаха',
    'ornithopter.flapping_angle.desc': 'Центральное смещение угла взмаха (-15..+15°)',
    'ornithopter.stroke_ferocity.title': 'Свирепость Удара',
    'ornithopter.stroke_ferocity.desc': 'Контроль интенсивности удара (0-100%)',
    'ornithopter.return_ferocity.title': 'Свирепость Возврата',
    'ornithopter.return_ferocity.desc': 'Контроль интенсивности возврата (0-100%)',
    'ornithopter.channel_test.title': 'Тест Каналов',
    'ornithopter.channel_test.override_off': 'Override Выкл',
    'ornithopter.channel_test.hint': 'Используйте override для ручного управления каналами',
    'ornithopter.channel_test.spring': 'Пружина',
    'ornithopter.channel_test.hold': 'Удерживать',
    'ornithopter.mixer_output.title': 'Выход Микшера',
    'ornithopter.mixer_output.override': 'Override',
    'ornithopter.mixer_output.left_wing': 'Левое Крыло',
    'ornithopter.mixer_output.right_wing': 'Правое Крыло',
EOF
            ;;
        en|ja|ko|zh|ar|hi)
            # English and non-translated languages use English values
            cat >> "$temp_file" << 'EOF'
    'app.menu.flight_profiles': 'Flight Profiles',
    'app.menu.link': 'Link',
    'app.menu.system': 'System',
    'app.menu.backup': 'Backup',
    'app.menu.diagnostics': 'Diagnostics',
    'elrs.info.section_pteronautos': 'PteronautOS',
    'elrs.info.model_name': 'Model Name',
    'elrs.info.ornithopter_mode': 'Ornithopter Mode',
    'elrs.info.active': 'Active',
    'elrs.info.kernel_type': 'Kernel Type',
    'elrs.info.kernel_servo': 'Kernel Servo',
    'elrs.info.mixer_profile': 'Mixer Profile',
    'elrs.info.zephyrus': 'Zephyrus Gyro',
    'elrs.info.link': 'Link',
    'elrs.info.section_device': 'Device',
    'ornithopter.kernel.model_name': 'Model Name',
    'ornithopter.kernel.model_name_desc': 'Custom name for this pterosaur model',
    'ornithopter.kernel.model_name_placeholder': 'e.g. Archaeopteryx 135',
    'ornithopter.kernel.servo_speed': 'Servo Speed',
    'ornithopter.kernel.servo_speed_desc': 'Maximum servo rotation speed (°/sec)',
    'ornithopter.kernel.base_freq': 'Base Flap Frequency',
    'ornithopter.kernel.base_freq_desc': 'Wing beat frequency at neutral stick (Hz)',
    'ornithopter.kernel.servo_min_us': 'Servo Min Pulse',
    'ornithopter.kernel.servo_pulse_desc': 'Pulse width limits for servo range (µs)',
    'ornithopter.kernel.servo_max_us': 'Servo Max Pulse',
    'ornithopter.kernel.trim_title': 'Servo Trim',
    'ornithopter.kernel.trim_desc': 'Center offset for each servo in microseconds',
    'ornithopter.servo.left_wing': 'Left Wing',
    'ornithopter.servo.right_wing': 'Right Wing',
    'ornithopter.servo.rudder': 'Rudder',
    'ornithopter.mixing.kernel_hint': 'Kernel handles waveform generation; Mixer handles control → servo mapping',
    'ornithopter.loading_config': 'Loading configuration...',
    'ornithopter.flight_profiles.per_profile_tuning': 'Per-Profile Tuning',
    'ornithopter.flight_profiles.profile_n': 'Profile {n}',
    'ornithopter.flight_profiles.active_ch7': 'Active via CH7',
    'ornithopter.flight_profiles.freq': 'Frequency',
    'ornithopter.flight_profiles.profile': 'Profile',
    'ornithopter.glide_angle.title': 'Glide Angle',
    'ornithopter.glide_angle.desc': 'Glide angle center offset (-15..+15°)',
    'ornithopter.flapping_angle.title': 'Flapping Angle',
    'ornithopter.flapping_angle.desc': 'Flapping angle center offset (-15..+15°)',
    'ornithopter.stroke_ferocity.title': 'Stroke Ferocity',
    'ornithopter.stroke_ferocity.desc': 'Controls intensity of power stroke (0-100%)',
    'ornithopter.return_ferocity.title': 'Return Ferocity',
    'ornithopter.return_ferocity.desc': 'Controls intensity of return stroke (0-100%)',
    'ornithopter.channel_test.title': 'Channel Test',
    'ornithopter.channel_test.override_off': 'Override Off',
    'ornithopter.channel_test.hint': 'Use override to manually control channels',
    'ornithopter.channel_test.spring': 'Spring',
    'ornithopter.channel_test.hold': 'Hold',
    'ornithopter.mixer_output.title': 'Mixer Output',
    'ornithopter.mixer_output.override': 'Override',
    'ornithopter.mixer_output.left_wing': 'Left Wing',
    'ornithopter.mixer_output.right_wing': 'Right Wing',
EOF
            ;;
    esac
    
    # Add closing bracket
    echo "})" >> "$temp_file"
    
    # Replace original file
    mv "$temp_file" "$file"
    
    echo "✓ $lang.js: keys added"
}

echo "Adding missing i18n keys..."

for lang in en de es fr pt ru ja ko zh ar hi; do
    add_keys $lang
done

echo ""
echo "✅ All i18n keys added!"
