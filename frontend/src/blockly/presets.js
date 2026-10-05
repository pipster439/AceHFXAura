/**
 * Pre-configured Blockly Workspace Templates for Lighting Effects & Orchestration
 */

export const EFFECT_PRESETS = [
  {
    id: 'template_smooth_breath',
    name: '双色平滑呼吸',
    description: '独立光效：全键盘在青色与紫红之间平滑呼吸往复循环（不依赖游戏）',
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
                type: 'color_cycle',
                inputs: {
                  COLOR_A: {
                    block: {
                      type: 'color_rgb',
                      inputs: {
                        R: { shadow: { type: 'math_number', fields: { NUM: 0 } } },
                        G: { shadow: { type: 'math_number', fields: { NUM: 180 } } },
                        B: { shadow: { type: 'math_number', fields: { NUM: 255 } } }
                      }
                    }
                  },
                  COLOR_B: {
                    block: {
                      type: 'color_rgb',
                      inputs: {
                        R: { shadow: { type: 'math_number', fields: { NUM: 220 } } },
                        G: { shadow: { type: 'math_number', fields: { NUM: 0 } } },
                        B: { shadow: { type: 'math_number', fields: { NUM: 160 } } }
                      }
                    }
                  },
                  PERIOD_SEC: {
                    shadow: { type: 'math_number', fields: { NUM: 3 } }
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
    id: 'template_low_health_warning',
    name: 'CS2 低血量警戒',
    description: '血量正常时全盘深海蓝，血量低于 25 时转为急促红黑呼吸闪烁',
    blocklyJson: {
      languageVersion: 0,
      blocks: [
        {
          type: 'controls_if',
          x: 60,
          y: 60,
          extraState: {
            hasElse: true
          },
          inputs: {
            IF0: {
              block: {
                type: 'gsi_player_health_condition',
                fields: {
                  OP: '<',
                  VALUE: 25
                }
              }
            },
            DO0: {
              block: {
                type: 'key_fill_all',
                inputs: {
                  COLOR: {
                    block: {
                      type: 'color_cycle',
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
                              G: { shadow: { type: 'math_number', fields: { NUM: 0 } } },
                              B: { shadow: { type: 'math_number', fields: { NUM: 0 } } }
                            }
                          }
                        },
                        PERIOD_SEC: {
                          shadow: { type: 'math_number', fields: { NUM: 0.8 } }
                        }
                      }
                    }
                  }
                }
              }
            },
            ELSE: {
              block: {
                type: 'key_fill_all',
                inputs: {
                  COLOR: {
                    block: {
                      type: 'color_rgb',
                      inputs: {
                        R: { shadow: { type: 'math_number', fields: { NUM: 0 } } },
                        G: { shadow: { type: 'math_number', fields: { NUM: 120 } } },
                        B: { shadow: { type: 'math_number', fields: { NUM: 255 } } }
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
    id: 'kill_wave',
    name: '击杀金色波纹',
    description: '以空格为中心向外迅速扩散的金色冲击波，用于击杀事件叠加',
    blocklyJson: {
      languageVersion: 0,
      blocks: [
        {
          type: 'key_ripple_effect',
          x: 60,
          y: 60,
          fields: {
            KEY: 'SPACE',
            SPEED: '5.0'
          },
          inputs: {
            COLOR: {
              block: {
                type: 'color_rgb',
                inputs: {
                  R: { shadow: { type: 'math_number', fields: { NUM: 255 } } },
                  G: { shadow: { type: 'math_number', fields: { NUM: 200 } } },
                  B: { shadow: { type: 'math_number', fields: { NUM: 0 } } }
                }
              }
            }
          }
        }
      ]
    }
  },
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
    name: '极光径向波浪',
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

// Small compositions reuse the supported block API instead of duplicating projects.
const number = value => ({ shadow: { type: 'math_number', fields: { NUM: value } } });
const rgb = (r, g, b) => ({ type: 'color_rgb', inputs: { R: number(r), G: number(g), B: number(b) } });
EFFECT_PRESETS.push(
  { id: 'template_static', name: '静态柔光', description: '全键盘固定青色，可调整 RGB 数值。',
    blocklyJson: { languageVersion: 0, blocks: [{ type: 'key_fill_all', inputs: { COLOR: { block: rgb(0, 100, 160) } } }] } },
  { id: 'template_reactive', name: '按键响应', description: '逐键按下后亮起、松开后衰减；预览使用模拟按键。',
    blocklyJson: { languageVersion: 0, blocks: [{ type: 'key_for_each', fields: { ZONE: 'all' }, inputs: { DO: { block: {
      type: 'key_set_color', inputs: { LED_ID: { block: { type: 'geometry_coords', fields: { FIELD: 'led_id' } } },
        COLOR: { block: { type: 'color_brightness', inputs: { COLOR: { block: rgb(0, 180, 255) }, SCALE: { block: { type: 'key_decay', inputs: { DECAY_RATE: number(3) } } } } } } }
    } } } }] } },
  { id: 'template_gradient', name: '简单双色渐变', description: '按横向位置在青色和紫色之间平滑过渡。',
    blocklyJson: { languageVersion: 0, blocks: [{ type: 'key_for_each', fields: { ZONE: 'all' }, inputs: { DO: { block: {
      type: 'key_set_color', inputs: { LED_ID: { block: { type: 'geometry_coords', fields: { FIELD: 'led_id' } } },
        COLOR: { block: { type: 'color_lerp', inputs: { COLOR_A: { block: rgb(0, 180, 255) }, COLOR_B: { block: rgb(180, 0, 255) },
          RATIO: { block: { type: 'math_arithmetic', fields: { OP: 'DIVIDE' }, inputs: { A: { block: { type: 'geometry_coords', fields: { FIELD: 'physical_x' } } }, B: number(16) } } } } } } }
    } } } }] } }
);
for (const preset of EFFECT_PRESETS) {
  const serialized = JSON.stringify(preset.blocklyJson);
  preset.tags = serialized.includes('gsi_') ? ['CS2', '遥测'] : serialized.includes('key_decay') ? ['按键', '响应'] : ['光效', '本地预览'];
  preset.requiredInputs = serialized.includes('gsi_') ? ['gsi_player_state'] : serialized.includes('key_decay') ? ['key_press_simulation'] : [];
  preset.capabilities = ['blockly', 'local_simulation', 'cpp_build'];
}
