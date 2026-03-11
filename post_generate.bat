@echo off
git -C "%~dp0." submodule update --init --recursive
