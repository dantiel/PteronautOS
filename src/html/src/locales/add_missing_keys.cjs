const fs = require('fs');
const path = require('path');

const KEYS_TO_ADD = {
    // Menu Groups
    'app.menu.flight_profiles': {
        de: 'Flugprofile', es: 'Perfiles de Vuelo', fr: 'Profils de Vol',
        pt: 'Perfis de Voo', ru: 'Профили Полёта'
    },
    'app.menu.link': {
        de: 'Link', es: 'Enlace', fr: 'Liaison', pt: 'Link', ru: 'Связь'
    },
    'app.menu.system': {
        de: 'System', es: 'Sistema', fr: 'Système', pt: 'Sistema', ru: 'Система'
    },
    'app.menu.backup': {
        de: 'Backup', es: 'Respaldo', fr: 'Sauvegarde', pt: 'Backup', ru: 'Резерв'
    },
    'app.menu.diagnostics': {
        de: 'Diagnose', es: 'Diagnóstico', fr: 'Diagnostic', pt: 'Diagnóstico', ru: 'Диагностика'
    },
    // Info Panel
    'elrs.info.section_pteronautos': {
        de: 'PteronautOS', es: 'PteronautOS', fr: 'PteronautOS', pt: 'PteronautOS', ru: 'PteronautOS'
    },
    'elrs.info.model_name': {
        de: 'Modellname', es: 'Nombre del Modelo', fr: 'Nom du Modèle', pt: 'Nome do Modelo', ru: 'Название Модели'
    },
    'elrs.info.ornithopter_mode': {
        de: 'Ornithopter Modus', es: 'Modo Ornithopter', fr: 'Mode Ornithoptère', pt: 'Modo Ornithopter', ru: 'Режим Орнитоптера'
    },
    'elrs.info.active': {
        de: 'Aktiv', es: 'Activo', fr: 'Actif', pt: 'Ativo', ru: 'Активен'
    },
    'elrs.info.kernel_type': {
        de: 'Kernel-Typ', es: 'Tipo de Kernel', fr: 'Type de Noyau', pt: 'Tipo de Kernel', ru: 'Тип Ядра'
    },
    'elrs.info.kernel_servo': {
        de: 'Kernel-Servo', es: 'Kernel-Servo', fr: 'Noyau-Servo', pt: 'Kernel-Servo', ru: 'Серво Ядра'
    },
    'elrs.info.mixer_profile': {
        de: 'Mixer-Profil', es: 'Perfil del Mixer', fr: 'Profil du Mixer', pt: 'Perfil do Mixer', ru: 'Профиль Микшера'
    },
    'elrs.info.zephyrus': {
        de: 'Zephyrus Gyro', es: 'Giroscopio Zephyrus', fr: 'Gyro Zephyrus', pt: 'Giroscópio Zephyrus', ru: 'Гироскоп Зефир'
    },
    'elrs.info.link': {
        de: 'Link', es: 'Enlace', fr: 'Liaison', pt: 'Link', ru: 'Связь'
    },
    'elrs.info.section_device': {
        de: 'Gerät', es: 'Dispositivo', fr: 'Appareil', pt: 'Dispositivo', ru: 'Устройство'
    },
    // Ornithopter Kernel
    'ornithopter.kernel.model_name': {
        de: 'Modellname', es: 'Nombre del Modelo', fr: 'Nom du Modèle', pt: 'Nome do Modelo', ru: 'Название Модели'
    },
    'ornithopter.kernel.model_name_desc': {
        de: 'Benutzerdefinierter Name für dieses Pterosaurier-Modell',
        es: 'Nombre personalizado para este modelo de pterosaurio',
        fr: "Nom personnalisé pour ce modèle de ptérosaure",
        pt: 'Nome personalizado para este modelo de pterossauro',
        ru: 'Пользовательское название этой модели птерозавра'
    },
    'ornithopter.kernel.model_name_placeholder': {
        de: 'z.B. Archaeopteryx 135', es: 'ej. Archaeopteryx 135', fr: 'ex. Archaeopteryx 135',
        pt: 'ex. Archaeopteryx 135', ru: 'напр. Archaeopteryx 135'
    },
    'ornithopter.kernel.servo_speed': {
        de: 'Servo-Geschwindigkeit', es: 'Velocidad del Servo', fr: 'Vitesse du Servo',
        pt: 'Velocidade do Servo', ru: 'Скорость Серво'
    },
    'ornithopter.kernel.servo_speed_desc': {
        de: 'Maximale Servo-Drehgeschwindigkeit (°/s)',
        es: 'Velocidad máxima de rotación del servo (°/s)',
        fr: 'Vitesse maximale de rotation du servo (°/s)',
        pt: 'Velocidade máxima de rotação do servo (°/s)',
        ru: 'Максимальная скорость вращения сервопривода (°/s)'
    },
    'ornithopter.kernel.base_freq': {
        de: 'Basis-Schlagfrequenz', es: 'Frecuencia Base de Aleteo', fr: 'Fréquence de Base de Battement',
        pt: 'Frequência Base de Batida', ru: 'Базовая Частота Взмаха'
    },
    'ornithopter.kernel.base_freq_desc': {
        de: 'Schlagfrequenz bei neutralem Stick (Hz)',
        es: 'Frecuencia de aleteo en stick neutral (Hz)',
        fr: 'Fréquence de battement au stick neutre (Hz)',
        pt: 'Frequência de batida no stick neutro (Hz)',
        ru: 'Частота взмаха при нейтральном стике (Гц)'
    },
    'ornithopter.kernel.servo_min_us': {
        de: 'Servo Min Impuls', es: 'Pulso Mínimo del Servo', fr: 'Impulsion Min du Servo',
        pt: 'Pulso Mínimo do Servo', ru: 'Мин Импульс Серво'
    },
    'ornithopter.kernel.servo_pulse_desc': {
        de: 'Impulsbreitengrenzen für den Servobereich (µs)',
        es: 'Límites de ancho de pulso para el rango del servo (µs)',
        fr: "Limites de largeur d'impulsion pour la plage du servo (µs)",
        pt: 'Limites de largura de pulso para o intervalo do servo (µs)',
        ru: 'Пределы ширины импульса для диапазона сервопривода (мкс)'
    },
    'ornithopter.kernel.servo_max_us': {
        de: 'Servo Max Impuls', es: 'Pulso Máximo del Servo', fr: 'Impulsion Max du Servo',
        pt: 'Pulso Máximo do Servo', ru: 'Макс Импульс Серво'
    },
    'ornithopter.kernel.trim_title': {
        de: 'Servo-Trim', es: 'Ajuste del Servo', fr: 'Réglage du Servo',
        pt: 'Ajuste do Servo', ru: 'Трим Серво'
    },
    'ornithopter.kernel.trim_desc': {
        de: 'Mittenversatz für jedes Servo in Mikrosekunden',
        es: 'Desplazamiento central para cada servo en microsegundos',
        fr: 'Décalage central pour chaque servo en microsecondes',
        pt: 'Deslocamento central para cada servo em microssegundos',
        ru: 'Центральное смещение для каждого сервопривода в микросекундах'
    },
    // Ornithopter Servo
    'ornithopter.servo.left_wing': {
        de: 'Linker Flügel', es: 'Ala Izquierda', fr: 'Aile Gauche', pt: 'Asa Esquerda', ru: 'Левое Крыло'
    },
    'ornithopter.servo.right_wing': {
        de: 'Rechter Flügel', es: 'Ala Derecha', fr: 'Aile Droite', pt: 'Asa Direita', ru: 'Правое Крыло'
    },
    'ornithopter.servo.rudder': {
        de: 'Seitenruder', es: 'Timón', fr: 'Gouvernail', pt: 'Leme', ru: 'Руль'
    },
    // Ornithopter Mixing
    'ornithopter.mixing.kernel_hint': {
        de: 'Kernel erzeugt Wellenform; Mixer steuert Servo-Zuordnung',
        es: 'Kernel genera forma de onda; Mixer controla mapeo de servos',
        fr: "Le noyau génère la forme d'onde; le mixer contrôle le mappage des servos",
        pt: 'Kernel gera forma de onda; Mixer controla mapeamento de servos',
        ru: 'Ядро генерирует форму волны; Микшер управляет отображением сервоприводов'
    },
    // Ornithopter Loading
    'ornithopter.loading_config': {
        de: 'Lade Konfiguration...', es: 'Cargando configuración...', fr: 'Chargement de la configuration...',
        pt: 'Carregando configuração...', ru: 'Загрузка конфигурации...'
    },
    // Flight Profiles
    'ornithopter.flight_profiles.per_profile_tuning': {
        de: 'Profilspezifische Einstellungen', es: 'Ajustes por Perfil', fr: 'Réglages par Profil',
        pt: 'Ajustes por Perfil', ru: 'Настройки для Профиля'
    },
    'ornithopter.flight_profiles.profile_n': {
        de: 'Profil {n}', es: 'Perfil {n}', fr: 'Profil {n}', pt: 'Perfil {n}', ru: 'Профиль {n}'
    },
    'ornithopter.flight_profiles.active_ch7': {
        de: 'Aktiv via CH7', es: 'Activo via CH7', fr: 'Actif via CH7', pt: 'Ativo via CH7', ru: 'Активен через CH7'
    },
    'ornithopter.flight_profiles.freq': {
        de: 'Frequenz', es: 'Frecuencia', fr: 'Fréquence', pt: 'Frequência', ru: 'Частота'
    },
    'ornithopter.flight_profiles.profile': {
        de: 'Profil', es: 'Perfil', fr: 'Profil', pt: 'Perfil', ru: 'Профиль'
    },
    // Glide/Flapping Angle
    'ornithopter.glide_angle.title': {
        de: 'Gleitwinkel', es: 'Ángulo de Planeo', fr: 'Angle de Plané', pt: 'Ângulo de Planeio', ru: 'Угол Планирования'
    },
    'ornithopter.glide_angle.desc': {
        de: 'Gleitwinkel-Mittenoffset (-15..+15°)',
        es: 'Desplazamiento central del ángulo de planeo (-15..+15°)',
        fr: "Décalage central de l'angle de plané (-15..+15°)",
        pt: 'Deslocamento central do ângulo de planeio (-15..+15°)',
        ru: 'Центральное смещение угла планирования (-15..+15°)'
    },
    'ornithopter.flapping_angle.title': {
        de: 'Schlagwinkel', es: 'Ángulo de Aleteo', fr: 'Angle de Battement', pt: 'Ângulo de Batida', ru: 'Угол Взмаха'
    },
    'ornithopter.flapping_angle.desc': {
        de: 'Schlagwinkel-Mittenoffset (-15..+15°)',
        es: 'Desplazamiento central del ángulo de aleteo (-15..+15°)',
        fr: "Décalage central de l'angle de battement (-15..+15°)",
        pt: 'Deslocamento central do ângulo de batida (-15..+15°)',
        ru: 'Центральное смещение угла взмаха (-15..+15°)'
    },
    // Stroke/Return Ferocity
    'ornithopter.stroke_ferocity.title': {
        de: 'Schlag-Wucht', es: 'Fiereza del Golpe', fr: 'Férocité du Coup', pt: 'Ferocidade do Golpe', ru: 'Свирепость Удара'
    },
    'ornithopter.stroke_ferocity.desc': {
        de: 'Steuerung der Schlagintensität (0-100%)',
        es: 'Control de la intensidad del golpe (0-100%)',
        fr: "Contrôle de l'intensité du coup (0-100%)",
        pt: 'Controle da intensidade do golpe (0-100%)',
        ru: 'Контроль интенсивности удара (0-100%)'
    },
    'ornithopter.return_ferocity.title': {
        de: 'Rückzug-Wucht', es: 'Fiereza del Retorno', fr: 'Férocité du Retour', pt: 'Ferocidade do Retorno', ru: 'Свирепость Возврата'
    },
    'ornithopter.return_ferocity.desc': {
        de: 'Steuerung der Rückzugintensität (0-100%)',
        es: 'Control de la intensidad del retorno (0-100%)',
        fr: "Contrôle de l'intensité du retour (0-100%)",
        pt: 'Controle da intensidade do retorno (0-100%)',
        ru: 'Контроль интенсивности возврата (0-100%)'
    },
    // Channel Test
    'ornithopter.channel_test.title': {
        de: 'Kanal-Test', es: 'Prueba de Canales', fr: 'Test de Canaux', pt: 'Teste de Canais', ru: 'Тест Каналов'
    },
    'ornithopter.channel_test.override_off': {
        de: 'Override Aus', es: 'Override Desactivado', fr: 'Override Désactivé', pt: 'Override Desativado', ru: 'Override Выкл'
    },
    'ornithopter.channel_test.hint': {
        de: 'Verwende Override um Kanäle manuell zu steuern',
        es: 'Usa override para controlar canales manualmente',
        fr: "Utilisez l'override pour contrôler les canaux manuellement",
        pt: 'Use override para controlar canais manualmente',
        ru: 'Используйте override для ручного управления каналами'
    },
    'ornithopter.channel_test.spring': {
        de: 'Feder', es: 'Resorte', fr: 'Ressort', pt: 'Mola', ru: 'Пружина'
    },
    'ornithopter.channel_test.hold': {
        de: 'Halten', es: 'Mantener', fr: 'Maintenir', pt: 'Manter', ru: 'Удерживать'
    },
    // Mixer Output
    'ornithopter.mixer_output.title': {
        de: 'Mixer-Ausgang', es: 'Salida del Mixer', fr: 'Sortie du Mixer', pt: 'Saída do Mixer', ru: 'Выход Микшера'
    },
    'ornithopter.mixer_output.override': {
        de: 'Override', es: 'Override', fr: 'Override', pt: 'Override', ru: 'Override'
    },
    'ornithopter.mixer_output.left_wing': {
        de: 'Linker Flügel', es: 'Ala Izquierda', fr: 'Aile Gauche', pt: 'Asa Esquerda', ru: 'Левое Крыло'
    },
    'ornithopter.mixer_output.right_wing': {
        de: 'Rechter Flügel', es: 'Ala Derecha', fr: 'Aile Droite', pt: 'Asa Direita', ru: 'Правое Крыло'
    }
};

const LANGS = ['en', 'de', 'es', 'fr', 'pt', 'ru', 'ja', 'ko', 'zh', 'ar', 'hi'];

for (const lang of LANGS) {
    const filePath = path.join(__dirname, `${lang}.js`);
    
    if (!fs.existsSync(filePath)) {
        console.log(`  ${lang}.js: not found`);
        continue;
    }
    
    // Read and parse the file
    let content = fs.readFileSync(filePath, 'utf-8');
    
    // Extract the export object
    const match = content.match(/export\s+default\s+(\{[\s\S]*\})\s*;?\s*$/);
    if (!match) {
        console.log(`  ${lang}.js: cannot parse export object`);
        continue;
    }
    
    try {
        // Parse the object
        const obj = eval(`(${match[1]})`);
        
        // Add missing keys
        let added = 0;
        for (const [key, translations] of Object.entries(KEYS_TO_ADD)) {
            if (!(key in obj)) {
                if (lang in translations) {
                    obj[key] = translations[lang];
                } else {
                    // Fallback to English
                    const parts = key.split('.');
                    obj[key] = parts[parts.length - 1].replace(/_/g, ' ').replace(/\b\w/g, l => l.toUpperCase());
                }
                added++;
            }
        }
        
        // Rebuild the file
        const entries = Object.entries(obj).map(([k, v]) => {
            const escapedValue = v.replace(/\\/g, '\\\\').replace(/'/g, "\\'");
            return `    '${k}': '${escapedValue}',`;
        });
        
        const newContent = `export default {\n${entries.join('\n')}\n};\n`;
        
        fs.writeFileSync(filePath, newContent, 'utf-8');
        console.log(`✓ ${lang}.js: ${added} keys added`);
    } catch (err) {
        console.log(`  ${lang}.js: parse error - ${err.message}`);
    }
}

console.log('\n✅ All i18n keys added!');
