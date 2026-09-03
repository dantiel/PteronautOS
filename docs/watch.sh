#!/bin/bash
# PteronautOS docs watcher — auto-rebuilds on .haml/.sass/.json changes
cd "$(dirname "$0")"
unset RUBYOPT
export BUNDLE_GEMFILE="$PWD/Gemfile"
exec bundle exec rake watch
