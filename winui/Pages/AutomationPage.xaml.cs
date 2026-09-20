using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Aura_WinUI.Services;

namespace Aura_WinUI.Pages;

public sealed class AutomationRuleItemViewModel
{
    public int Index { get; set; }
    public string Process { get; set; } = "";
    public string Profile { get; set; } = "";
}

public sealed partial class AutomationPage : Page
{
    private readonly IAuraControlClient _client;
    private string _currentRevision = "";
    private readonly List<string> _availableProfiles = new();
    private readonly List<AutomationRuleDto> _rules = new();

    public AutomationPage()
    {
        InitializeComponent();
        _client = AuraControlClient.Instance;
        Loaded += AutomationPage_Loaded;
    }

    private async void AutomationPage_Loaded(object sender, RoutedEventArgs e)
    {
        await LoadDataAsync();
    }

    private async void RefreshBtn_Click(object sender, RoutedEventArgs e)
    {
        await LoadDataAsync();
    }

    private async Task LoadDataAsync(bool preserveInfoBar = false)
    {
        LoadingBar.Visibility = Visibility.Visible;
        if (!preserveInfoBar)
        {
            PageInfoBar.IsOpen = false;
        }

        try
        {
            // 1. 获取可用方案列表 (作为规则绑定的权威可选范围)
            var profileResult = await _client.GetProfilesAsync();
            if (profileResult.IsSuccess)
            {
                _availableProfiles.Clear();
                foreach (var p in profileResult.Profiles)
                {
                    _availableProfiles.Add(p.Name);
                }
            }

            // 2. 获取当前前台进程与守护进程运行态
            var statusResult = await _client.GetRuntimeStatusAsync();
            if (statusResult.IsOnline && statusResult.Data?.Runtime != null)
            {
                var fg = statusResult.Data.Runtime.ForegroundProcess;
                ForegroundProcText.Text = string.IsNullOrEmpty(fg) ? "桌面 / 待机" : fg;
            }
            else
            {
                ForegroundProcText.Text = "Daemon 离线";
            }

            // 3. 获取自动化规则列表
            var rulesResult = await _client.GetAutomationRulesAsync();
            if (!rulesResult.IsSuccess)
            {
                ShowInfoBar("读取规则失败", rulesResult.ErrorMessage, InfoBarSeverity.Error);
                ConfigRevisionText.Text = "--";
                RuleCountText.Text = "--";
                return;
            }

            _currentRevision = rulesResult.Revision;
            _rules.Clear();
            _rules.AddRange(rulesResult.Rules);

            ConfigRevisionText.Text = _currentRevision;
            RuleCountText.Text = _rules.Count.ToString();

            // 4. 绑定至 UI 列表 (直接使用服务端权威 raw rule 数组索引)
            var viewModels = new List<AutomationRuleItemViewModel>();
            foreach (var rule in _rules)
            {
                viewModels.Add(new AutomationRuleItemViewModel
                {
                    Index = rule.Index,
                    Process = rule.Process,
                    Profile = rule.Profile
                });
            }

            RulesListControl.ItemsSource = viewModels;
            EmptyStateCard.Visibility = viewModels.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        }
        catch (Exception ex)
        {
            ShowInfoBar("加载异常", ex.Message, InfoBarSeverity.Error);
        }
        finally
        {
            LoadingBar.Visibility = Visibility.Collapsed;
        }
    }

    private async void AddRuleBtn_Click(object sender, RoutedEventArgs e)
    {
        if (_availableProfiles.Count == 0)
        {
            // 重新尝试加载方案
            var pRes = await _client.GetProfilesAsync();
            if (pRes.IsSuccess)
            {
                _availableProfiles.Clear();
                _availableProfiles.AddRange(pRes.Profiles.Select(p => p.Name));
            }
        }

        if (_availableProfiles.Count == 0)
        {
            ShowInfoBar("无法添加规则", "未能获取到任何灯效方案，请确认配置文件中已定义 profiles。", InfoBarSeverity.Warning);
            return;
        }

        // 构建添加规则对话框
        var stack = new StackPanel { Spacing = 16 };

        var processBox = new TextBox
        {
            Header = "前台触发程序名 (Process)",
            PlaceholderText = "例如: cs2.exe 或 cs2",
            HorizontalAlignment = HorizontalAlignment.Stretch
        };

        var profileCombo = new ComboBox
        {
            Header = "激活方案 (Profile)",
            ItemsSource = _availableProfiles,
            SelectedIndex = 0,
            HorizontalAlignment = HorizontalAlignment.Stretch
        };

        var tipText = new TextBlock
        {
            Text = "支持自动补齐 .exe 后缀。当系统检测到此前台窗口激活时，将自动切换至指定灯效方案。",
            Style = (Style)Application.Current.Resources["CaptionTextBlockStyle"],
            Foreground = (Microsoft.UI.Xaml.Media.Brush)Application.Current.Resources["TextFillColorSecondaryBrush"],
            TextWrapping = TextWrapping.Wrap
        };

        stack.Children.Add(processBox);
        stack.Children.Add(profileCombo);
        stack.Children.Add(tipText);

        var dialog = new ContentDialog
        {
            Title = "添加应用联动规则",
            Content = stack,
            PrimaryButtonText = "确认添加",
            CloseButtonText = "取消",
            DefaultButton = ContentDialogButton.Primary,
            XamlRoot = this.XamlRoot
        };

        var result = await dialog.ShowAsync();
        if (result != ContentDialogResult.Primary)
        {
            return;
        }

        string rawProcess = processBox.Text.Trim();
        string selectedProfile = profileCombo.SelectedItem as string ?? "";

        if (string.IsNullOrWhiteSpace(rawProcess))
        {
            ShowInfoBar("输入校验失败", "程序名不能为空", InfoBarSeverity.Error);
            return;
        }

        if (string.IsNullOrWhiteSpace(selectedProfile))
        {
            ShowInfoBar("输入校验失败", "请选择绑定的灯效方案", InfoBarSeverity.Error);
            return;
        }

        LoadingBar.Visibility = Visibility.Visible;
        try
        {
            var mutation = await _client.AddAutomationRuleAsync(
                new AutomationRuleDto { Process = rawProcess, Profile = selectedProfile },
                _currentRevision);

            if (mutation.IsSuccess)
            {
                ShowInfoBar("规则已添加", $"成功为程序 \"{mutation.Rule?.Process ?? rawProcess}\" 绑定方案 \"{selectedProfile}\"", InfoBarSeverity.Success);
                await LoadDataAsync(preserveInfoBar: true);
            }
            else if (mutation.IsConflict)
            {
                ShowInfoBar("版本冲突 (409 Conflict)", "配置已被外部修改，已自动刷新至最新配置，请重新尝试操作。", InfoBarSeverity.Warning);
                await LoadDataAsync(preserveInfoBar: true);
            }
            else if (mutation.IsDuplicate)
            {
                ShowInfoBar("重复规则 (409 Conflict)", mutation.ErrorMessage, InfoBarSeverity.Error);
            }
            else
            {
                ShowInfoBar("添加失败", mutation.ErrorMessage, InfoBarSeverity.Error);
            }
        }
        catch (Exception ex)
        {
            ShowInfoBar("操作异常", ex.Message, InfoBarSeverity.Error);
        }
        finally
        {
            LoadingBar.Visibility = Visibility.Collapsed;
        }
    }

    private async void EditRuleItem_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button btn || btn.Tag is not AutomationRuleItemViewModel item)
        {
            return;
        }

        if (_availableProfiles.Count == 0)
        {
            var pRes = await _client.GetProfilesAsync();
            if (pRes.IsSuccess)
            {
                _availableProfiles.Clear();
                _availableProfiles.AddRange(pRes.Profiles.Select(p => p.Name));
            }
        }

        var stack = new StackPanel { Spacing = 16 };

        var processBox = new TextBox
        {
            Header = "前台触发程序名 (Process)",
            Text = item.Process,
            HorizontalAlignment = HorizontalAlignment.Stretch
        };

        var profileCombo = new ComboBox
        {
            Header = "激活方案 (Profile)",
            ItemsSource = _availableProfiles,
            SelectedItem = item.Profile,
            HorizontalAlignment = HorizontalAlignment.Stretch
        };

        if (profileCombo.SelectedIndex < 0 && _availableProfiles.Count > 0)
        {
            profileCombo.SelectedIndex = 0;
        }

        stack.Children.Add(processBox);
        stack.Children.Add(profileCombo);

        var dialog = new ContentDialog
        {
            Title = $"编辑规则 #{item.Index + 1}",
            Content = stack,
            PrimaryButtonText = "保存修改",
            CloseButtonText = "取消",
            DefaultButton = ContentDialogButton.Primary,
            XamlRoot = this.XamlRoot
        };

        var result = await dialog.ShowAsync();
        if (result != ContentDialogResult.Primary)
        {
            return;
        }

        string newProcess = processBox.Text.Trim();
        string newProfile = profileCombo.SelectedItem as string ?? "";

        if (string.IsNullOrWhiteSpace(newProcess))
        {
            ShowInfoBar("输入校验失败", "程序名不能为空", InfoBarSeverity.Error);
            return;
        }

        LoadingBar.Visibility = Visibility.Visible;
        try
        {
            var mutation = await _client.UpdateAutomationRuleAsync(
                item.Index,
                newProcess,
                newProfile,
                _currentRevision);

            if (mutation.IsSuccess)
            {
                ShowInfoBar("规则已更新", $"规则 #{item.Index + 1} 已成功修改为 \"{mutation.Rule?.Process ?? newProcess} -> {newProfile}\"", InfoBarSeverity.Success);
                await LoadDataAsync(preserveInfoBar: true);
            }
            else if (mutation.IsConflict)
            {
                ShowInfoBar("版本冲突 (409 Conflict)", "配置已被外部修改，已自动刷新至最新配置，请重新尝试操作。", InfoBarSeverity.Warning);
                await LoadDataAsync(preserveInfoBar: true);
            }
            else if (mutation.IsDuplicate)
            {
                ShowInfoBar("重复规则 (409 Conflict)", mutation.ErrorMessage, InfoBarSeverity.Error);
            }
            else
            {
                ShowInfoBar("更新失败", mutation.ErrorMessage, InfoBarSeverity.Error);
            }
        }
        catch (Exception ex)
        {
            ShowInfoBar("操作异常", ex.Message, InfoBarSeverity.Error);
        }
        finally
        {
            LoadingBar.Visibility = Visibility.Collapsed;
        }
    }

    private async void DeleteRuleItem_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button btn || btn.Tag is not AutomationRuleItemViewModel item)
        {
            return;
        }

        var dialog = new ContentDialog
        {
            Title = "删除规则确认",
            Content = $"确定要删除应用联动规则 \"{item.Process} -> {item.Profile}\" 吗？此操作将立即写入配置文件。",
            PrimaryButtonText = "确认删除",
            CloseButtonText = "取消",
            DefaultButton = ContentDialogButton.Close,
            XamlRoot = this.XamlRoot
        };

        var result = await dialog.ShowAsync();
        if (result != ContentDialogResult.Primary)
        {
            return;
        }

        LoadingBar.Visibility = Visibility.Visible;
        try
        {
            var mutation = await _client.DeleteAutomationRuleAsync(item.Index, _currentRevision);
            if (mutation.IsSuccess)
            {
                ShowInfoBar("规则已删除", $"规则 \"{item.Process}\" 已从配置文件中移除", InfoBarSeverity.Success);
                await LoadDataAsync(preserveInfoBar: true);
            }
            else if (mutation.IsConflict)
            {
                ShowInfoBar("版本冲突 (409 Conflict)", "配置已被外部修改，已自动刷新至最新配置，请重新尝试操作。", InfoBarSeverity.Warning);
                await LoadDataAsync(preserveInfoBar: true);
            }
            else
            {
                ShowInfoBar("删除失败", mutation.ErrorMessage, InfoBarSeverity.Error);
            }
        }
        catch (Exception ex)
        {
            ShowInfoBar("操作异常", ex.Message, InfoBarSeverity.Error);
        }
        finally
        {
            LoadingBar.Visibility = Visibility.Collapsed;
        }
    }

    private void ShowInfoBar(string title, string message, InfoBarSeverity severity)
    {
        PageInfoBar.Title = title;
        PageInfoBar.Message = message;
        PageInfoBar.Severity = severity;
        PageInfoBar.IsOpen = true;
    }
}
