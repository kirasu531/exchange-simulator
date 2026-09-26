setarch "$(uname -m)" -R \
env TSAN_OPTIONS=halt_on_error=1 \
ctest --test-dir build-tsan --output-on-failure
