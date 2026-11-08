#!/usr/bin/env bash
# Convert the fine-tuned model to the ggml format the Shabd plugin loads, and
# quantise it (q5_0: ~550 MB, near-identical accuracy, much faster).
#
#   bash export_ggml.sh out/shabd-moji
#
# Produces out/shabd-moji/ggml-shabd-moji-q5_0.bin. Copy it to
# Documents/Shabd/models/ and Shabd picks it automatically (any file with
# "shabd" in the name is preferred over the stock model).
set -euo pipefail

RUN_DIR=${1:?usage: export_ggml.sh <finetune output dir>}
NAME=$(basename "$RUN_DIR")
WORK=$(mktemp -d)

git clone --depth 1 -b v1.9.5 https://github.com/ggml-org/whisper.cpp "$WORK/whisper.cpp"
git clone --depth 1 https://github.com/openai/whisper "$WORK/openai-whisper" # mel filters + tokenizer assets

python "$WORK/whisper.cpp/models/convert-h5-to-ggml.py" "$RUN_DIR/hf" "$WORK/openai-whisper" "$RUN_DIR"
mv "$RUN_DIR/ggml-model.bin" "$RUN_DIR/ggml-$NAME-f16.bin"

cmake -S "$WORK/whisper.cpp" -B "$WORK/build" -DCMAKE_BUILD_TYPE=Release -DWHISPER_BUILD_TESTS=OFF > /dev/null
cmake --build "$WORK/build" --target whisper-quantize -j > /dev/null
"$WORK/build/bin/whisper-quantize" "$RUN_DIR/ggml-$NAME-f16.bin" "$RUN_DIR/ggml-$NAME-q5_0.bin" q5_0

echo
echo "Done: $RUN_DIR/ggml-$NAME-q5_0.bin"
echo "Copy it to Documents/Shabd/models/ and reopen Shabd."
