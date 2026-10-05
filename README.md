# NaturalCenter Tune

Corrector de tono que corrige solo el **centro de afinación** de cada nota y preserva vibrato y expresión.

## Compilar
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release          # descarga JUCE 8.0.6
    cmake --build build --config Release                     # VST3 (+AU en macOS) + Standalone
Opciones: `-DAAX_SDK_PATH=/ruta/AAX_SDK` (activa AAX; requiere firma PACE), `-DJUCE_DIR_LOCAL=/ruta/JUCE`,
`-DNCT_BUILD_TESTS=ON` (test DSP sin JUCE: `nct_dsp_test`).
Linux: libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libfreetype-dev libfontconfig1-dev libgl-dev.

## Latencia (reportada al host)
Real-time ~26 ms (voces desde 90 Hz) · High Quality ~44 ms (desde 60 Hz) a 48 kHz.
