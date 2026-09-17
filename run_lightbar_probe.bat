@echo off
chcp 65001 > nul
title ROG FALCHION ACE HFX - 顶部 15-LED Light Bar 独立控制探针
cd /d "%~dp0"
python lightbar_probe.py
pause
