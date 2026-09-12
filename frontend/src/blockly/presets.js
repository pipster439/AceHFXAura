/**
 * Pre-configured Blockly Workspace Templates for Lighting Effects & Orchestration
 */

export const EFFECT_PRESETS = [
  {
    id: 'cs2_health_bar',
    name: 'CS2 动态血条联动',
    description: '通过 GSI 读取玩家实时血量：满血亮绿，残血变红，红光闪烁',
    code: `// CS2 Dynamic Health Reactive
double hp = 100.0;
if (gsi != nullptr) {
    hp = gsi->GetNumber("player.state.health", 100.0);
}
const double factor = std::clamp(hp / 100.0, 0.0, 1.0);
const uint8_t r = static_cast<uint8_t>(std::clamp((1.0 - factor) * 255.0, 0.0, 255.0));
const uint8_t g = static_cast<uint8_t>(std::clamp(factor * 255.0, 0.0, 255.0));
out_frame.Fill(r, g, 0);`,
    blocklyJson: {
      languageVersion: 0,
      blocks: [
        {
          type: 'key_fill_all',
          x: 60,
          y: 60,
          inputs: {
            COLOR: {
              block: {
                type: 'color_lerp',
                inputs: {
                  COLOR_A: {
                    block: {
                      type: 'color_rgb',
                      inputs: {
                        R: { shadow: { type: 'math_number', fields: { NUM: 255 } } },
                        G: { shadow: { type: 'math_number', fields: { NUM: 0 } } },
                        B: { shadow: { type: 'math_number', fields: { NUM: 0 } } }
                      }
                    }
                  },
                  COLOR_B: {
                    block: {
                      type: 'color_rgb',
                      inputs: {
                        R: { shadow: { type: 'math_number', fields: { NUM: 0 } } },
                        G: { shadow: { type: 'math_number', fields: { NUM: 255 } } },
                        B: { shadow: { type: 'math_number', fields: { NUM: 0 } } }
                      }
                    }
                  },
                  RATIO: {
                    block: {
                      type: 'math_arithmetic',
                      fields: { OP: 'DIVIDE' },
                      inputs: {
                        A: {
                          block: {
                            type: 'gsi_get_number',
                            fields: { PATH: 'player.state.health' },
                            inputs: {
                              DEFAULT: { shadow: { type: 'math_number', fields: { NUM: 100 } } }
                            }
                          }
                        },
                        B: { shadow: { type: 'math_number', fields: { NUM: 100 } } }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      ]
    }
  },
  {
    id: 'rainbow_radial_wave',
    name: '极光径向波浪 (Rainbow Radial)',
    description: '以键盘中心为圆心扩散的彩虹涟漪环',
    blocklyJson: {
      languageVersion: 0,
      blocks: [
        {
          type: 'key_for_each',
          x: 60,
          y: 60,
          fields: { ZONE: 'all' },
          inputs: {
            DO: {
              block: {
                type: 'key_set_color',
                inputs: {
                  LED_ID: {
                    block: {
                      type: 'geometry_coords',
                      fields: { FIELD: 'led_id' }
                    }
                  },
                  COLOR: {
                    block: {
                      type: 'color_hsv',
                      inputs: {
                        H: {
                          block: {
                            type: 'math_arithmetic',
                            fields: { OP: 'MULTIPLY' },
                            inputs: {
                              A: {
                                block: {
                                  type: 'geometry_radial_phase',
                                  inputs: {
                                    CX: { shadow: { type: 'math_number', fields: { NUM: 7.5 } } },
                                    CY: { shadow: { type: 'math_number', fields: { NUM: 3.0 } } },
                                    WAVELENGTH: { shadow: { type: 'math_number', fields: { NUM: 5.0 } } },
                                    SPEED: { shadow: { type: 'math_number', fields: { NUM: 1.5 } } }
                                  }
                                }
                              },
                              B: { shadow: { type: 'math_number', fields: { NUM: 360 } } }
                            }
                          }
                        },
                        S: { shadow: { type: 'math_number', fields: { NUM: 100 } } },
                        V: { shadow: { type: 'math_number', fields: { NUM: 100 } } }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      ]
    }
  },
  {
    id: 'wasd_radar_highlight',
    name: 'WASD 战术雷达高亮',
    description: 'WASD 战术金色常亮，其余键位呈现深海蓝呼吸',
    blocklyJson: {
      languageVersion: 0,
      blocks: [
        {
          type: 'key_fill_all',
          x: 60,
          y: 60,
          inputs: {
            COLOR: {
              block: {
                type: 'color_rgb',
                inputs: {
                  R: { shadow: { type: 'math_number', fields: { NUM: 0 } } },
                  G: { shadow: { type: 'math_number', fields: { NUM: 30 } } },
                  B: { shadow: { type: 'math_number', fields: { NUM: 80 } } }
                }
              }
            }
          },
          next: {
            block: {
              type: 'key_for_each',
              fields: { ZONE: 'wasd' },
              inputs: {
                DO: {
                  block: {
                    type: 'key_set_color',
                    inputs: {
                      LED_ID: {
                        block: {
                          type: 'geometry_coords',
                          fields: { FIELD: 'led_id' }
                        }
                      },
                      COLOR: {
                        block: {
                          type: 'color_rgb',
                          inputs: {
                            R: { shadow: { type: 'math_number', fields: { NUM: 255 } } },
                            G: { shadow: { type: 'math_number', fields: { NUM: 180 } } },
                            B: { shadow: { type: 'math_number', fields: { NUM: 0 } } }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      ]
    }
  }
];

export const ORCHESTRATOR_PRESETS = [
  {
    id: 'scratch_flow_preset',
    name: 'Scratch 智能控制流',
    description: '使用 Scratch「如果...那么...」语法块：匹配前台 cs2.exe 切换至竞技方案并叠加击杀光效',
    blocklyJson: {
      languageVersion: 0,
      blocks: [
        {
          type: 'orch_root_flow',
          x: 50,
          y: 50,
          fields: { FALLBACK_PROFILE: 'desktop' },
          inputs: {
            DO: {
              block: {
                type: 'controls_if',
                inputs: {
                  IF0: {
                    block: {
                      type: 'orch_text_equals',
                      inputs: {
                        A: { block: { type: 'orch_current_process' } },
                        B: { shadow: { type: 'text', fields: { TEXT: 'cs2.exe' } } }
                      }
                    }
                  },
                  DO0: {
                    block: {
                      type: 'orch_action_switch_profile',
                      fields: { PROFILE: 'cs2_gamer' },
                      next: {
                        block: {
                          type: 'orch_action_set_dnd',
                          fields: { DND: 'TRUE' },
                          next: {
                            block: {
                              type: 'orch_action_overlay_pulse',
                              fields: {
                                EVENT: 'event.kill',
                                EFFECT: 'rainbow_wave',
                                DURATION: 1200,
                                FADE: 400,
                                PRIORITY: 20
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      ]
    }
  },
  {
    id: 'cs2_competitive_preset',
    name: '经典进程编排',
    description: '前台 cs2.exe 自动切换至竞技方案，支持击杀金色脉冲与致盲白屏覆盖',
    blocklyJson: {
      languageVersion: 0,
      blocks: [
        {
          type: 'orchestrator_root',
          x: 50,
          y: 50,
          fields: { FALLBACK_PROFILE: 'desktop' },
          inputs: {
            OVERLAYS: {
              block: {
                type: 'event_overlay',
                fields: {
                  EVENT: 'event.kill',
                  EFFECT: 'kill_pulse',
                  DURATION: 1200,
                  FADE: 400,
                  PRIORITY: 30
                },
                next: {
                  block: {
                    type: 'event_overlay',
                    fields: {
                      EVENT: 'event.flash',
                      EFFECT: 'white_flash',
                      DURATION: 2000,
                      FADE: 1000,
                      PRIORITY: 50
                    }
                  }
                }
              }
            },
            RULES: {
              block: {
                type: 'match_process',
                fields: {
                  PROCESS: 'cs2.exe',
                  TARGET_PROFILE: 'cs2_gamer',
                  DND: 'TRUE'
                },
                inputs: {
                  CONDITION: {
                    block: {
                      type: 'condition_compare',
                      fields: {
                        FIELD: 'player.state.health',
                        OP: '>',
                        VALUE: '0'
                      }
                    }
                  }
                }
              }
            }
          }
        }
      ]
    }
  }
];
