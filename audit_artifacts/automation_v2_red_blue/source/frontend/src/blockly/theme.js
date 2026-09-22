import Blockly from './index.js';

/**
 * MD3E Theme for Google Blockly
 * Uses CSS variables mapped from MD3E tokens for automatic dark/light mode sync.
 */
export const MD3EBlocklyTheme = Blockly.Theme.defineTheme('md3e', {
  name: 'md3e',
  base: Blockly.Themes.Classic,
  blockStyles: {
    clock_blocks: {
      colourPrimary: '#6750A4',
      colourSecondary: '#EADDFF',
      colourTertiary: '#4F378B'
    },
    geometry_blocks: {
      colourPrimary: '#7D5260',
      colourSecondary: '#FFD8E4',
      colourTertiary: '#633B48'
    },
    color_blocks: {
      colourPrimary: '#006874',
      colourSecondary: '#9EEFFD',
      colourTertiary: '#004F58'
    },
    keyboard_blocks: {
      colourPrimary: '#386A20',
      colourSecondary: '#B7F397',
      colourTertiary: '#205107'
    },
    dynamics_blocks: {
      colourPrimary: '#984061',
      colourSecondary: '#FFD9E2',
      colourTertiary: '#7D2949'
    },
    gsi_blocks: {
      colourPrimary: '#BA1A1A',
      colourSecondary: '#FFDAD6',
      colourTertiary: '#93000A'
    },
    root_blocks: {
      colourPrimary: '#4355B9',
      colourSecondary: '#DEE0FF',
      colourTertiary: '#2B3C9F'
    },
    overlay_blocks: {
      colourPrimary: '#825500',
      colourSecondary: '#FFDDB3',
      colourTertiary: '#634000'
    },
    process_blocks: {
      colourPrimary: '#006877',
      colourSecondary: '#A1EFFF',
      colourTertiary: '#004F5B'
    },
    condition_blocks: {
      colourPrimary: '#705D00',
      colourSecondary: '#FFE264',
      colourTertiary: '#544600'
    }
  },
  categoryStyles: {
    clock_category: { colour: '#6750A4' },
    geometry_category: { colour: '#7D5260' },
    color_category: { colour: '#006874' },
    keyboard_category: { colour: '#386A20' },
    dynamics_category: { colour: '#984061' },
    gsi_category: { colour: '#BA1A1A' },
    root_category: { colour: '#4355B9' },
    overlay_category: { colour: '#825500' },
    process_category: { colour: '#006877' },
    condition_category: { colour: '#705D00' }
  },
  componentStyles: {
    workspaceBackgroundColour: 'transparent',
    toolboxBackgroundColour: 'var(--md-sys-color-surface-container-low, #1e1e24)',
    toolboxForegroundColour: 'var(--md-sys-color-on-surface, #e6e1e5)',
    flyoutBackgroundColour: 'var(--md-sys-color-surface-container, #282830)',
    flyoutForegroundColour: 'var(--md-sys-color-on-surface, #e6e1e5)',
    flyoutOpacity: 0.95,
    scrollbarColour: 'var(--md-sys-color-outline-variant, #49454f)',
    scrollbarOpacity: 0.6,
    insertionMarkerColour: 'var(--md-sys-color-primary, #d0bcff)',
    insertionMarkerOpacity: 0.5,
    cursorColour: 'var(--md-sys-color-primary, #d0bcff)'
  },
  fontStyle: {
    family: '"Google Sans", "Noto Sans SC", system-ui, -apple-system, sans-serif',
    weight: '500',
    size: 12
  }
});

/**
 * Standard injection options adhering to zero-network and singlefile constraints
 */
export const DEFAULT_INJECT_OPTIONS = {
  theme: MD3EBlocklyTheme,
  sounds: false,
  media: 'data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7',
  trashcan: true,
  collapse: false,
  comments: true,
  disable: true,
  grid: {
    spacing: 20,
    length: 3,
    colour: '#353540',
    snap: true
  },
  zoom: {
    controls: true,
    wheel: true,
    startScale: 1.0,
    maxScale: 2.5,
    minScale: 0.4,
    scaleSpeed: 1.2
  },
  renderer: 'geras'
};
