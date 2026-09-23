// Close transient Blockly UI before an embedded host panel covers the editor.
// Keep the workspace and its unsaved blocks intact.
export function dismissForOverlay(workspace) {
  if (!workspace) return () => {};
  const toolbox = workspace.getToolbox();
  if (toolbox) {
    toolbox.clearSelection();
    toolbox.getFlyout()?.hide();
    workspace.hideChaff();
    return () => {};
  }

  // Automation uses a permanently visible, category-free flyout. Temporarily
  // mask it and restore its prior visibility when the host panel closes.
  workspace.hideChaff(true);
  const flyout = workspace.getFlyout();
  const wasVisible = flyout?.isVisible() ?? false;
  flyout?.setVisible(false);
  return () => { if (wasVisible) flyout?.setVisible(true); };
}
