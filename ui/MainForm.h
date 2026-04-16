#pragma once

#include "..\src\core_exports.h"

#include <vcclr.h>
#include <vector>

// Win32 的 ExtractAssociatedIcon 宏会覆盖 System::Drawing::Icon::ExtractAssociatedIcon，先取消它
#ifdef ExtractAssociatedIcon
#undef ExtractAssociatedIcon
#endif

#using < System.dll>
#using < System.Drawing.dll>
#using < System.Windows.Forms.dll>

namespace fvpyuki
{

    using namespace System;
    using namespace System::ComponentModel;
    using namespace System::Diagnostics;
    using namespace System::Drawing;
    using namespace System::Drawing::Drawing2D;
    using namespace System::IO;
    using namespace System::Reflection;
    using namespace System::Runtime::InteropServices;
    using namespace System::Windows::Forms;

public
    enum class UiWorkKind
    {
        Extract = 0,
        RepackText = 1,
        RepackBin = 2,
        BuildPatch = 3,
    };

    ref class WorkRequest
    {
    public:
        UiWorkKind kind = UiWorkKind::Extract;
        array<String ^> ^ files;
        String ^ sourceHcbPath = String::Empty;
        String ^ translationPath = String::Empty;
        String ^ outputPath = String::Empty;
        String ^ translationFormat = String::Empty;
        String ^ textEncoding = L"gbk";
        String ^ manifestOrDir = String::Empty;
        String ^ patchInputRoot = String::Empty;
        String ^ patchOutputDir = String::Empty;
        bool autoRebuildImages = false;
        bool includeUnchanged = false;
    };

    ref class WorkResult
    {
    public:
        int handledCount = 0;
        int successCount = 0;
        int failureCount = 0;
        String ^ summary = String::Empty;
        String ^ detail = String::Empty;
        String ^ primaryPath = String::Empty;
        String ^ secondaryPath = String::Empty;
    };

    // 配色：淡蓝 + 樱粉，贴合星辰恋曲的主题
    ref class Palette abstract sealed
    {
    public:
        static initonly Color WindowBg = Color::FromArgb(0xF6, 0xF8, 0xFD);
        static initonly Color HeaderBg = Color::FromArgb(0xEA, 0xF1, 0xFB);
        static initonly Color CardBg = Color::FromArgb(0xFF, 0xFF, 0xFF);
        static initonly Color CardBorder = Color::FromArgb(0xD9, 0xE4, 0xF5);
        static initonly Color DropBg = Color::FromArgb(0xF3, 0xF7, 0xFE);
        static initonly Color DropBorder = Color::FromArgb(0xB8, 0xD2, 0xF0);
        static initonly Color Primary = Color::FromArgb(0x9F, 0xC3, 0xE8);
        static initonly Color PrimaryHover = Color::FromArgb(0xB8, 0xD3, 0xEE);
        static initonly Color PrimaryText = Color::FromArgb(0xFF, 0xFF, 0xFF);
        static initonly Color Secondary = Color::FromArgb(0xEE, 0xB3, 0xCB);
        static initonly Color SecondaryHover = Color::FromArgb(0xF3, 0xC8, 0xD9);
        static initonly Color Ghost = Color::FromArgb(0xEA, 0xF1, 0xFA);
        static initonly Color GhostHover = Color::FromArgb(0xD9, 0xE5, 0xF3);
        static initonly Color GhostText = Color::FromArgb(0x44, 0x59, 0x7B);
        static initonly Color TextPrimary = Color::FromArgb(0x2F, 0x3F, 0x58);
        static initonly Color TextSecondary = Color::FromArgb(0x75, 0x87, 0xA3);
        static initonly Color Accent = Color::FromArgb(0xCF, 0x7E, 0xA0);
        static initonly Color BandOverlay = Color::FromArgb(190, 0xFF, 0xFF, 0xFF);
    };

    // 真正透明的 Panel：会把父控件的背景（含背景图和 Paint 事件）透过来
    ref class TransparentPanel : public Panel
    {
    public:
        TransparentPanel()
        {
            this->SetStyle(ControlStyles::SupportsTransparentBackColor | ControlStyles::OptimizedDoubleBuffer | ControlStyles::AllPaintingInWmPaint | ControlStyles::UserPaint, true);
            this->BackColor = Color::Transparent;
            this->DoubleBuffered = true;
        }

    protected:
        void OnPaintBackground(PaintEventArgs ^ e) override
        {
            if (this->Parent == nullptr)
            {
                return;
            }
            auto state = e->Graphics->Save();
            e->Graphics->TranslateTransform((float)-this->Left, (float)-this->Top);
            System::Drawing::Rectangle parentRect(this->Left, this->Top, this->Width, this->Height);
            PaintEventArgs ^ pe = gcnew PaintEventArgs(e->Graphics, parentRect);
            this->InvokePaintBackground(this->Parent, pe);
            this->InvokePaint(this->Parent, pe);
            e->Graphics->Restore(state);
        }
    };

    // 圆角卡片：在 Panel 之上加一圈淡淡的边框
    ref class RoundedCard : public Panel
    {
    public:
        int CornerRadius = 14;
        Color BorderColor = Palette::CardBorder;
        Color FillColor = Palette::CardBg;
        int BorderThickness = 1;

        RoundedCard()
        {
            this->DoubleBuffered = true;
            this->BackColor = Color::Transparent;
            this->SetStyle(ControlStyles::SupportsTransparentBackColor | ControlStyles::AllPaintingInWmPaint | ControlStyles::OptimizedDoubleBuffer | ControlStyles::ResizeRedraw | ControlStyles::UserPaint, true);
        }

    protected:
        void OnPaint(PaintEventArgs ^ e) override
        {
            Graphics ^ g = e->Graphics;
            g->SmoothingMode = SmoothingMode::AntiAlias;

            System::Drawing::Rectangle rect(0, 0, this->Width - 1, this->Height - 1);
            GraphicsPath ^ path = BuildPath(rect, CornerRadius);

            // 用显式 ARGB 填充，这样半透明才能生效
            SolidBrush ^ fill = gcnew SolidBrush(FillColor);
            g->FillPath(fill, path);
            delete fill;

            // 画一圈柔和的描边
            Pen ^ pen = gcnew Pen(BorderColor, (float)BorderThickness);
            g->DrawPath(pen, path);
            delete pen;

            delete path;
        }

        void OnPaintBackground(PaintEventArgs ^ e) override
        {
            // 让父控件把自己的背景画到我们的客户区，圆角缺口就能透出背景图和叠加层
            if (this->Parent != nullptr)
            {
                auto state = e->Graphics->Save();
                e->Graphics->TranslateTransform((float)-this->Left, (float)-this->Top);
                System::Drawing::Rectangle parentRect(this->Left, this->Top, this->Width, this->Height);
                PaintEventArgs ^ pe = gcnew PaintEventArgs(e->Graphics, parentRect);
                this->InvokePaintBackground(this->Parent, pe);
                this->InvokePaint(this->Parent, pe);
                e->Graphics->Restore(state);
            }
            else
            {
                e->Graphics->Clear(Palette::WindowBg);
            }
        }

        internal : static GraphicsPath ^ BuildPath(System::Drawing::Rectangle r, int radius)
        {
            int d = radius * 2;
            if (d > r.Width)
                d = r.Width;
            if (d > r.Height)
                d = r.Height;
            GraphicsPath ^ p = gcnew GraphicsPath();
            p->AddArc((float)r.X, (float)r.Y, (float)d, (float)d, 180.0f, 90.0f);
            p->AddArc((float)(r.Right - d), (float)r.Y, (float)d, (float)d, 270.0f, 90.0f);
            p->AddArc((float)(r.Right - d), (float)(r.Bottom - d), (float)d, (float)d, 0.0f, 90.0f);
            p->AddArc((float)r.X, (float)(r.Bottom - d), (float)d, (float)d, 90.0f, 90.0f);
            p->CloseFigure();
            return p;
        }
    };

    // 扁平圆角按钮，鼠标悬停时颜色会过渡一下
    ref class SoftButton : public Button
    {
    public:
        Color BaseColor = Palette::Primary;
        Color HoverColor = Palette::PrimaryHover;
        Color TextColor = Palette::PrimaryText;
        int CornerRadius = 10;

        SoftButton()
        {
            this->FlatStyle = System::Windows::Forms::FlatStyle::Flat;
            this->FlatAppearance->BorderSize = 0;
            this->Cursor = Cursors::Hand;
            this->DoubleBuffered = true;
            this->SetStyle(ControlStyles::AllPaintingInWmPaint | ControlStyles::OptimizedDoubleBuffer | ControlStyles::ResizeRedraw | ControlStyles::UserPaint, true);
            this->TextAlign = ContentAlignment::MiddleCenter;
            this->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 10.0f, FontStyle::Bold);

            this->MouseEnter += gcnew EventHandler(this, &SoftButton::OnHoverEnter);
            this->MouseLeave += gcnew EventHandler(this, &SoftButton::OnHoverLeave);
            this->EnabledChanged += gcnew EventHandler(this, &SoftButton::OnEnabledChanged);

            this->BackColor = BaseColor;
            this->ForeColor = TextColor;
        }

    protected:
        void OnPaint(PaintEventArgs ^ e) override
        {
            Graphics ^ g = e->Graphics;
            g->SmoothingMode = SmoothingMode::AntiAlias;
            g->TextRenderingHint = ::System::Drawing::Text::TextRenderingHint::ClearTypeGridFit;

            System::Drawing::Rectangle rect(0, 0, this->Width - 1, this->Height - 1);
            GraphicsPath ^ path = RoundedCard::BuildPath(rect, CornerRadius);

            Color fillColor = this->Enabled ? this->BackColor : Color::FromArgb(0xEE, 0xE3, 0xEA);
            SolidBrush ^ brush = gcnew SolidBrush(fillColor);
            g->FillPath(brush, path);
            delete brush;

            // 背景绘制被关掉了，文字要手动画
            Color textColor = this->Enabled ? this->ForeColor : Palette::TextSecondary;
            TextRenderer::DrawText(g, this->Text, this->Font, this->ClientRectangle, textColor,
                                   TextFormatFlags::HorizontalCenter | TextFormatFlags::VerticalCenter | TextFormatFlags::NoPadding);

            delete path;
        }

        void OnPaintBackground(PaintEventArgs ^ e) override
        {
            // 把父控件的背景整体透过来，圆角才能正确和底图融合
            if (this->Parent != nullptr)
            {
                auto state = e->Graphics->Save();
                e->Graphics->TranslateTransform((float)-this->Left, (float)-this->Top);
                System::Drawing::Rectangle parentRect(this->Left, this->Top, this->Width, this->Height);
                PaintEventArgs ^ pe = gcnew PaintEventArgs(e->Graphics, parentRect);
                this->InvokePaintBackground(this->Parent, pe);
                this->InvokePaint(this->Parent, pe);
                e->Graphics->Restore(state);
            }
            else
            {
                e->Graphics->Clear(Palette::WindowBg);
            }
        }

    private:
        void OnHoverEnter(Object ^ sender, EventArgs ^ e)
        {
            if (this->Enabled)
            {
                this->BackColor = HoverColor;
            }
        }
        void OnHoverLeave(Object ^ sender, EventArgs ^ e)
        {
            if (this->Enabled)
            {
                this->BackColor = BaseColor;
            }
        }
        void OnEnabledChanged(Object ^ sender, EventArgs ^ e)
        {
            this->BackColor = BaseColor;
            this->Invalidate();
        }
    };

public
    ref class MainForm : public Form
    {
    public:
        MainForm()
        {
            InitializeComponent();
            InitializeIcon();
            RefreshPackDefaults(true);
            SetLastOpenTarget(GetDefaultUnpackFullPath());
            SetIdleState(L"等候指令中～ =￣ω￣=", L"把 .hcb / .bin 拖进来开始吧。或切到 Pack 页签回写翻译。");
            StartFadeIn();
        }

    protected:
        ~MainForm() override {}

    private:
        // 顶部标题栏和整体框架
        TransparentPanel ^ headerPanel;
        PictureBox ^ logoBox;
        Label ^ brandTitleLabel;
        Label ^ brandSubtitleLabel;

        SoftButton ^ extractTabButton;
        SoftButton ^ packTabButton;
        TransparentPanel ^ extractPanel;
        TransparentPanel ^ packPanel;

        // 解包页签的控件
        RoundedCard ^ dropCard;
        Label ^ dropTitleLabel;
        Label ^ dropHintLabel;
        Label ^ outputRootLabel;
        Label ^ extractInfoLabel;

        // 封包页签的控件
        Label ^ packIntroLabel;
        SoftButton ^ detectDefaultsButton;

        RoundedCard ^ textPackCard;
        Label ^ textPackTitle;
        TextBox ^ sourceHcbTextBox;
        TextBox ^ translationFileTextBox;
        TextBox ^ outputHcbTextBox;
        ComboBox ^ translationFormatComboBox;
        ComboBox ^ textEncodingComboBox;
        SoftButton ^ browseSourceHcbButton;
        SoftButton ^ browseTranslationButton;
        SoftButton ^ browseOutputHcbButton;
        SoftButton ^ repackTextButton;

        RoundedCard ^ binPackCard;
        Label ^ binPackTitle;
        ComboBox ^ manifestComboBox;
        TextBox ^ outputBinTextBox;
        CheckBox ^ autoRebuildImagesCheckBox;
        SoftButton ^ refreshManifestButton;
        SoftButton ^ browseManifestButton;
        SoftButton ^ browseOutputBinButton;
        SoftButton ^ repackBinButton;

        RoundedCard ^ patchPackCard;
        Label ^ patchPackTitle;
        TextBox ^ patchOutputDirTextBox;
        CheckBox ^ patchAutoRebuildCheckBox;
        CheckBox ^ includeUnchangedCheckBox;
        SoftButton ^ browsePatchOutputDirButton;
        SoftButton ^ buildPatchButton;

        // 底部状态栏
        Label ^ statusLabel;
        ProgressBar ^ progressBar;
        Label ^ currentLogTitleLabel;
        TextBox ^ currentLogBox;
        SoftButton ^ openResultButton;
        SoftButton ^ resetStateButton;

        Timer ^ progressTimer;
        Timer ^ fadeTimer;
        BackgroundWorker ^ worker;

        String ^ busyStatusPrefix = String::Empty;
        String ^ lastOpenTarget = String::Empty;

        // 样式快捷方法
        void StyleTextBox(TextBox ^ box)
        {
            box->BorderStyle = BorderStyle::FixedSingle;
            box->BackColor = Color::FromArgb(0xFF, 0xFA, 0xFC);
            box->ForeColor = Palette::TextPrimary;
            box->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 9.5f, FontStyle::Regular);
        }

        void StyleComboBox(ComboBox ^ box)
        {
            box->FlatStyle = System::Windows::Forms::FlatStyle::Flat;
            box->BackColor = Color::FromArgb(0xFF, 0xFA, 0xFC);
            box->ForeColor = Palette::TextPrimary;
            box->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 9.5f, FontStyle::Regular);
        }

        void StyleCheckBox(CheckBox ^ box)
        {
            box->AutoSize = true;
            box->BackColor = Color::Transparent;
            box->ForeColor = Palette::TextPrimary;
            box->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 9.5f, FontStyle::Regular);
            box->Cursor = Cursors::Hand;
        }

        Label ^ MakeFieldLabel(String ^ text, int x, int y)
        {
            Label ^ l = gcnew Label();
            l->AutoSize = true;
            l->BackColor = Color::Transparent;
            l->ForeColor = Palette::TextSecondary;
            l->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 9.0f, FontStyle::Regular);
            l->Location = Point(x, y);
            l->Text = text;
            return l;
        }

        Label ^ MakeCardTitle(String ^ text)
        {
            Label ^ l = gcnew Label();
            l->AutoSize = true;
            l->BackColor = Color::Transparent;
            l->ForeColor = Palette::Accent;
            l->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 11.0f, FontStyle::Bold);
            l->Text = text;
            return l;
        }

        SoftButton ^ MakePrimaryButton(String ^ text)
        {
            SoftButton ^ b = gcnew SoftButton();
            b->BaseColor = Palette::Primary;
            b->HoverColor = Palette::PrimaryHover;
            b->TextColor = Palette::PrimaryText;
            b->BackColor = b->BaseColor;
            b->ForeColor = b->TextColor;
            b->Text = text;
            return b;
        }

        SoftButton ^ MakeGhostButton(String ^ text)
        {
            SoftButton ^ b = gcnew SoftButton();
            b->BaseColor = Palette::Ghost;
            b->HoverColor = Palette::GhostHover;
            b->TextColor = Palette::GhostText;
            b->BackColor = b->BaseColor;
            b->ForeColor = b->TextColor;
            b->CornerRadius = 8;
            b->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 9.5f, FontStyle::Regular);
            b->Text = text;
            return b;
        }

        SoftButton ^ MakeSecondaryButton(String ^ text)
        {
            SoftButton ^ b = gcnew SoftButton();
            b->BaseColor = Palette::Secondary;
            b->HoverColor = Palette::SecondaryHover;
            b->TextColor = Palette::PrimaryText;
            b->BackColor = b->BaseColor;
            b->ForeColor = b->TextColor;
            b->Text = text;
            return b;
        }

        // 背景插画
        Bitmap ^ formBackdrop; // 预先按窗口客户区大小渲染好的底图

        void LoadBackgroundImage()
        {
            try
            {
                HMODULE hMod = ::GetModuleHandleW(nullptr);
                HRSRC hRes = ::FindResourceW(hMod, MAKEINTRESOURCEW(201), RT_RCDATA);
                if (hRes == nullptr)
                {
                    return;
                }
                HGLOBAL hGlob = ::LoadResource(hMod, hRes);
                if (hGlob == nullptr)
                {
                    return;
                }
                DWORD bytesLen = ::SizeofResource(hMod, hRes);
                void *bytesPtr = ::LockResource(hGlob);
                if (bytesPtr == nullptr || bytesLen == 0)
                {
                    return;
                }

                array<Byte> ^ buffer = gcnew array<Byte>(static_cast<int>(bytesLen));
                Marshal::Copy(IntPtr(bytesPtr), buffer, 0, static_cast<int>(bytesLen));
                MemoryStream ^ stream = gcnew MemoryStream(buffer);
                Image ^ source = Image::FromStream(stream);

                // 用 cover 方式（按较大的比例缩放裁剪）把底图铺满客户区，
                // 早期用 Min() 时窗口两侧留白太多，插画被挤成一小条
                int clientW = this->ClientSize.Width;
                int clientH = this->ClientSize.Height;
                formBackdrop = gcnew Bitmap(clientW, clientH);
                Graphics ^ g = Graphics::FromImage(formBackdrop);
                g->InterpolationMode = InterpolationMode::HighQualityBicubic;
                g->SmoothingMode = SmoothingMode::AntiAlias;
                g->Clear(Palette::WindowBg);

                float srcW = (float)source->Width;
                float srcH = (float)source->Height;
                float scale = System::Math::Max((float)clientW / srcW, (float)clientH / srcH);
                int drawW = (int)(srcW * scale);
                int drawH = (int)(srcH * scale);
                int drawX = (clientW - drawW) / 2;
                int drawY = (clientH - drawH) / 2;
                g->DrawImage(source, drawX, drawY, drawW, drawH);

                // 叠一层淡淡的白色蒙版，让控件文字更容易读
                SolidBrush ^ veil = gcnew SolidBrush(Color::FromArgb(105, 0xFF, 0xFF, 0xFF));
                g->FillRectangle(veil, 0, 0, clientW, clientH);
                delete veil;
                delete g;
                delete source;
            }
            catch (Exception ^)
            {
                // 背景图只是装饰，加载失败就当没有
                formBackdrop = nullptr;
            }
        }

        void PaintBackdropSlice(Graphics ^ g, Control ^ target)
        {
            if (formBackdrop == nullptr)
            {
                return;
            }
            // 把目标控件的位置换算到窗体客户区坐标
            System::Drawing::Point origin = target->PointToScreen(System::Drawing::Point(0, 0));
            System::Drawing::Point formOrigin = this->PointToScreen(System::Drawing::Point(0, 0));
            int offsetX = origin.X - formOrigin.X;
            int offsetY = origin.Y - formOrigin.Y;
            System::Drawing::Rectangle sourceRect(offsetX, offsetY, target->Width, target->Height);
            System::Drawing::Rectangle destRect(0, 0, target->Width, target->Height);
            g->DrawImage(formBackdrop, destRect, sourceRect, GraphicsUnit::Pixel);
        }

        void OnFormPaint(Object ^ sender, PaintEventArgs ^ e)
        {
            // 先画底图——Form.Paint 在系统清屏之后触发，这样底图一定显示得出来
            Graphics ^ g = e->Graphics;
            if (formBackdrop != nullptr)
            {
                g->DrawImage(formBackdrop, 0, 0, this->ClientSize.Width, this->ClientSize.Height);
            }
            // 在顶部标题和底部状态条位置再盖一层半透明白带，文字更清晰
            g->SmoothingMode = SmoothingMode::AntiAlias;
            SolidBrush ^ band = gcnew SolidBrush(Palette::BandOverlay);
            g->FillRectangle(band, 0, 0, this->ClientSize.Width, 76);
            g->FillRectangle(band, 0, 560, this->ClientSize.Width, this->ClientSize.Height - 560);
            delete band;
        }

        // 窗口图标与渐入动画
        void InitializeIcon()
        {
            try
            {
                String ^ exePath = Assembly::GetExecutingAssembly()->Location;
                System::Drawing::Icon ^ ico = System::Drawing::Icon::ExtractAssociatedIcon(exePath);
                if (ico != nullptr)
                {
                    this->Icon = ico;
                    Bitmap ^ bmp = gcnew Bitmap(48, 48);
                    Graphics ^ g = Graphics::FromImage(bmp);
                    g->SmoothingMode = SmoothingMode::AntiAlias;
                    g->InterpolationMode = InterpolationMode::HighQualityBicubic;
                    g->DrawIcon(ico, System::Drawing::Rectangle(0, 0, 48, 48));
                    delete g;
                    logoBox->Image = bmp;
                    logoBox->SizeMode = PictureBoxSizeMode::Zoom;
                }
            }
            catch (Exception ^)
            {
                // 图标加载失败不是致命问题，忽略
            }
        }

        void StartFadeIn()
        {
            this->Opacity = 0.0;
            fadeTimer = gcnew Timer();
            fadeTimer->Interval = 15;
            fadeTimer->Tick += gcnew EventHandler(this, &MainForm::OnFadeTick);
            fadeTimer->Start();
        }

        void OnFadeTick(Object ^ sender, EventArgs ^ e)
        {
            double next = this->Opacity + 0.06;
            if (next >= 1.0)
            {
                this->Opacity = 1.0;
                fadeTimer->Stop();
                return;
            }
            this->Opacity = next;
        }

        // 布局
        void InitializeComponent()
        {
            SuspendLayout();

            AutoScaleMode = System::Windows::Forms::AutoScaleMode::Font;
            AllowDrop = false;
            BackColor = Palette::WindowBg;
            ClientSize = Drawing::Size(920, 700);
            FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedSingle;
            MaximizeBox = false;
            MinimizeBox = true;
            StartPosition = FormStartPosition::CenterScreen;
            Text = L"FVP-Yuki  ♡  FVP引擎 解封包工具";
            Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 9.5f, FontStyle::Regular);
            DoubleBuffered = true;

            LoadBackgroundImage();
            this->Paint += gcnew PaintEventHandler(this, &MainForm::OnFormPaint);

            BuildHeader();
            BuildTabs();
            BuildStatusBar();

            Controls->Add(headerPanel);
            Controls->Add(extractTabButton);
            Controls->Add(packTabButton);
            Controls->Add(extractPanel);
            Controls->Add(packPanel);
            Controls->Add(statusLabel);
            Controls->Add(openResultButton);
            Controls->Add(resetStateButton);
            Controls->Add(progressBar);
            Controls->Add(currentLogTitleLabel);
            Controls->Add(currentLogBox);

            progressTimer = gcnew Timer();
            progressTimer->Interval = 120;
            progressTimer->Tick += gcnew EventHandler(this, &MainForm::OnProgressTimerTick);

            worker = gcnew BackgroundWorker();
            worker->DoWork += gcnew DoWorkEventHandler(this, &MainForm::OnWorkerDoWork);
            worker->RunWorkerCompleted += gcnew RunWorkerCompletedEventHandler(this, &MainForm::OnWorkerCompleted);

            ResumeLayout(false);
        }

        void BuildHeader()
        {
            headerPanel = gcnew TransparentPanel();
            headerPanel->Location = Point(0, 0);
            headerPanel->Size = Drawing::Size(920, 76);

            logoBox = gcnew PictureBox();
            logoBox->Location = Point(22, 14);
            logoBox->Size = Drawing::Size(48, 48);
            logoBox->BackColor = Color::Transparent;

            brandTitleLabel = gcnew Label();
            brandTitleLabel->AutoSize = false;
            brandTitleLabel->Location = Point(82, 12);
            brandTitleLabel->Size = Drawing::Size(600, 30);
            brandTitleLabel->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 16.0f, FontStyle::Bold);
            brandTitleLabel->ForeColor = Palette::Accent;
            brandTitleLabel->BackColor = Color::Transparent;
            brandTitleLabel->Text = L"FVP-Yuki  ♡  FVP引擎 解封包工具";

            brandSubtitleLabel = gcnew Label();
            brandSubtitleLabel->AutoSize = false;
            brandSubtitleLabel->Location = Point(84, 44);
            brandSubtitleLabel->Size = Drawing::Size(820, 22);
            brandSubtitleLabel->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 9.5f, FontStyle::Regular);
            brandSubtitleLabel->ForeColor = Palette::TextSecondary;
            brandSubtitleLabel->BackColor = Color::Transparent;
            brandSubtitleLabel->Text = L"拖拽 .hcb / .bin 即可解包，Pack 页签回写翻译与资源！✿";

            headerPanel->Controls->Add(logoBox);
            headerPanel->Controls->Add(brandTitleLabel);
            headerPanel->Controls->Add(brandSubtitleLabel);
        }

        void OnExtractTabClicked(Object ^ sender, EventArgs ^ e)
        {
            extractTabButton->BaseColor = Palette::Primary;
            extractTabButton->BackColor = Palette::PrimaryHover;
            packTabButton->BaseColor = Palette::Ghost;
            packTabButton->BackColor = Palette::Ghost;
            extractPanel->Visible = true;
            packPanel->Visible = false;
        }

        void OnPackTabClicked(Object ^ sender, EventArgs ^ e)
        {
            packTabButton->BaseColor = Palette::Primary;
            packTabButton->BackColor = Palette::PrimaryHover;
            extractTabButton->BaseColor = Palette::Ghost;
            extractTabButton->BackColor = Palette::Ghost;
            packPanel->Visible = true;
            extractPanel->Visible = false;
        }

        void BuildTabs()
        {
            extractTabButton = MakeGhostButton(L"  ✿  解包 Extract  ");
            extractTabButton->Location = Point(18, 86);
            extractTabButton->Size = Drawing::Size(150, 42);
            extractTabButton->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 10.0f, FontStyle::Bold);
            extractTabButton->Click += gcnew EventHandler(this, &MainForm::OnExtractTabClicked);

            packTabButton = MakeGhostButton(L"  ♡  封包 Pack  ");
            packTabButton->Location = Point(176, 86);
            packTabButton->Size = Drawing::Size(150, 42);
            packTabButton->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 10.0f, FontStyle::Bold);
            packTabButton->Click += gcnew EventHandler(this, &MainForm::OnPackTabClicked);

            extractPanel = gcnew TransparentPanel();
            extractPanel->Location = Point(18, 136);
            extractPanel->Size = Drawing::Size(884, 422);
            extractPanel->AllowDrop = true;
            extractPanel->DragEnter += gcnew DragEventHandler(this, &MainForm::OnDragEnter);
            extractPanel->DragDrop += gcnew DragEventHandler(this, &MainForm::OnDragDrop);

            packPanel = gcnew TransparentPanel();
            packPanel->Location = Point(18, 136);
            packPanel->Size = Drawing::Size(884, 422);
            packPanel->Visible = false; // 初始隐藏

            BuildExtractTab();
            BuildPackTab();

            // 默认停留在解包页签
            OnExtractTabClicked(nullptr, nullptr);
        }

        void BuildExtractTab()
        {
            dropCard = gcnew RoundedCard();
            dropCard->FillColor = Color::FromArgb(112, 0xFF, 0xFF, 0xFF);
            dropCard->BorderColor = Palette::DropBorder;
            dropCard->CornerRadius = 20;
            dropCard->Location = Point(22, 20);
            dropCard->Size = Drawing::Size(836, 270);
            dropCard->AllowDrop = true;
            dropCard->DragEnter += gcnew DragEventHandler(this, &MainForm::OnDragEnter);
            dropCard->DragDrop += gcnew DragEventHandler(this, &MainForm::OnDragDrop);

            dropTitleLabel = gcnew Label();
            dropTitleLabel->AutoSize = false;
            dropTitleLabel->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 20.0f, FontStyle::Bold);
            dropTitleLabel->ForeColor = Palette::Accent;
            dropTitleLabel->BackColor = Color::Transparent;
            dropTitleLabel->Location = Point(22, 56);
            dropTitleLabel->Size = Drawing::Size(790, 42);
            dropTitleLabel->Text = L"♡  把 .hcb 或 .bin 拖到这里  ♡";
            dropTitleLabel->TextAlign = ContentAlignment::MiddleCenter;

            dropHintLabel = gcnew Label();
            dropHintLabel->AutoSize = false;
            dropHintLabel->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 10.5f, FontStyle::Regular);
            dropHintLabel->ForeColor = Palette::TextSecondary;
            dropHintLabel->BackColor = Color::Transparent;
            dropHintLabel->Location = Point(60, 112);
            dropHintLabel->Size = Drawing::Size(716, 56);
            dropHintLabel->Text = L"程序会自动识别文件类型：\r\n.hcb → 导出到 unpack\\text     .bin → 导出到 unpack\\extracted_xxx";
            dropHintLabel->TextAlign = ContentAlignment::MiddleCenter;

            outputRootLabel = gcnew Label();
            outputRootLabel->AutoSize = false;
            outputRootLabel->Font = gcnew ::System::Drawing::Font(L"Consolas", 10.0f, FontStyle::Regular);
            outputRootLabel->ForeColor = Palette::TextPrimary;
            outputRootLabel->BackColor = Color::Transparent;
            outputRootLabel->Location = Point(24, 196);
            outputRootLabel->Size = Drawing::Size(788, 22);
            outputRootLabel->Text = L"输出目录: " + gcnew String(PackCppGetDefaultUnpackRoot());
            outputRootLabel->TextAlign = ContentAlignment::MiddleCenter;

            dropCard->Controls->Add(dropTitleLabel);
            dropCard->Controls->Add(dropHintLabel);
            dropCard->Controls->Add(outputRootLabel);

            extractInfoLabel = gcnew Label();
            extractInfoLabel->AutoSize = false;
            extractInfoLabel->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 9.5f, FontStyle::Regular);
            extractInfoLabel->ForeColor = Palette::TextSecondary;
            extractInfoLabel->BackColor = Color::Transparent;
            extractInfoLabel->Location = Point(22, 300);
            extractInfoLabel->Size = Drawing::Size(836, 96);
            extractInfoLabel->Text = L"小提示：\r\n① 先在这里解包，再切到 Pack 页签进行文本回写、单个 BIN 重封或一键构建补丁。\r\n② 直接使用 exe + dll 时，请把它们放到游戏根目录同层。";

            extractPanel->Controls->Add(dropCard);
            extractPanel->Controls->Add(extractInfoLabel);
        }

        void BuildPackTab()
        {
            packIntroLabel = gcnew Label();
            packIntroLabel->AutoSize = false;
            packIntroLabel->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 9.8f, FontStyle::Regular);
            packIntroLabel->ForeColor = Palette::TextSecondary;
            packIntroLabel->BackColor = Color::Transparent;
            packIntroLabel->Location = Point(22, 14);
            packIntroLabel->Size = Drawing::Size(680, 34);
            packIntroLabel->Text = L"推荐流程：改好 unpack\\text → 点「封包文本」；改好 unpack\\extracted_xxx → 点「重封 BIN」；\r\n需要整套产物 → 点「一键构建补丁」。";

            detectDefaultsButton = MakeGhostButton(L"刷新默认路径");
            detectDefaultsButton->Location = Point(720, 16);
            detectDefaultsButton->Size = Drawing::Size(140, 34);
            detectDefaultsButton->Click += gcnew EventHandler(this, &MainForm::OnRefreshDefaultsClicked);

            BuildTextPackCard();
            BuildBinPackCard();
            BuildPatchPackCard();

            packPanel->Controls->Add(packIntroLabel);
            packPanel->Controls->Add(detectDefaultsButton);
            packPanel->Controls->Add(textPackCard);
            packPanel->Controls->Add(binPackCard);
            packPanel->Controls->Add(patchPackCard);
        }

        void BuildTextPackCard()
        {
            textPackCard = gcnew RoundedCard();
            textPackCard->FillColor = Color::FromArgb(178, 0xFF, 0xFF, 0xFF);
            textPackCard->Location = Point(22, 58);
            textPackCard->Size = Drawing::Size(836, 170);

            textPackTitle = MakeCardTitle(L"✿  文本封包");
            textPackTitle->Location = Point(20, 14);

            sourceHcbTextBox = gcnew TextBox();
            StyleTextBox(sourceHcbTextBox);
            sourceHcbTextBox->Location = Point(98, 46);
            sourceHcbTextBox->Size = Drawing::Size(588, 24);

            browseSourceHcbButton = MakeGhostButton(L"浏览");
            browseSourceHcbButton->Location = Point(696, 44);
            browseSourceHcbButton->Size = Drawing::Size(68, 28);
            browseSourceHcbButton->Click += gcnew EventHandler(this, &MainForm::OnBrowseSourceHcbClicked);

            translationFileTextBox = gcnew TextBox();
            StyleTextBox(translationFileTextBox);
            translationFileTextBox->Location = Point(98, 78);
            translationFileTextBox->Size = Drawing::Size(588, 24);

            browseTranslationButton = MakeGhostButton(L"浏览");
            browseTranslationButton->Location = Point(696, 76);
            browseTranslationButton->Size = Drawing::Size(68, 28);
            browseTranslationButton->Click += gcnew EventHandler(this, &MainForm::OnBrowseTranslationClicked);

            outputHcbTextBox = gcnew TextBox();
            StyleTextBox(outputHcbTextBox);
            outputHcbTextBox->Location = Point(98, 110);
            outputHcbTextBox->Size = Drawing::Size(588, 24);

            browseOutputHcbButton = MakeGhostButton(L"另存");
            browseOutputHcbButton->Location = Point(696, 108);
            browseOutputHcbButton->Size = Drawing::Size(68, 28);
            browseOutputHcbButton->Click += gcnew EventHandler(this, &MainForm::OnBrowseOutputHcbClicked);

            translationFormatComboBox = gcnew ComboBox();
            StyleComboBox(translationFormatComboBox);
            translationFormatComboBox->DropDownStyle = ComboBoxStyle::DropDownList;
            translationFormatComboBox->Location = Point(98, 140);
            translationFormatComboBox->Size = Drawing::Size(110, 25);
            translationFormatComboBox->Items->AddRange(gcnew array<Object ^>{L"auto", L"jsonl", L"legacy"});
            translationFormatComboBox->SelectedIndex = 0;

            textEncodingComboBox = gcnew ComboBox();
            StyleComboBox(textEncodingComboBox);
            textEncodingComboBox->DropDownStyle = ComboBoxStyle::DropDownList;
            textEncodingComboBox->Location = Point(288, 140);
            textEncodingComboBox->Size = Drawing::Size(110, 25);
            textEncodingComboBox->Items->AddRange(gcnew array<Object ^>{L"gbk", L"shift_jis"});
            textEncodingComboBox->SelectedIndex = 0;

            repackTextButton = MakePrimaryButton(L"封包文本  ♡");
            repackTextButton->Location = Point(624, 136);
            repackTextButton->Size = Drawing::Size(140, 32);
            repackTextButton->Click += gcnew EventHandler(this, &MainForm::OnRepackTextClicked);

            textPackCard->Controls->Add(textPackTitle);
            textPackCard->Controls->Add(MakeFieldLabel(L"源 HCB", 22, 50));
            textPackCard->Controls->Add(sourceHcbTextBox);
            textPackCard->Controls->Add(browseSourceHcbButton);
            textPackCard->Controls->Add(MakeFieldLabel(L"翻译文件", 22, 82));
            textPackCard->Controls->Add(translationFileTextBox);
            textPackCard->Controls->Add(browseTranslationButton);
            textPackCard->Controls->Add(MakeFieldLabel(L"输出 HCB", 22, 114));
            textPackCard->Controls->Add(outputHcbTextBox);
            textPackCard->Controls->Add(browseOutputHcbButton);
            textPackCard->Controls->Add(MakeFieldLabel(L"输入格式", 22, 144));
            textPackCard->Controls->Add(translationFormatComboBox);
            textPackCard->Controls->Add(MakeFieldLabel(L"写回编码", 222, 144));
            textPackCard->Controls->Add(textEncodingComboBox);
            textPackCard->Controls->Add(repackTextButton);
        }

        void BuildBinPackCard()
        {
            binPackCard = gcnew RoundedCard();
            binPackCard->FillColor = Color::FromArgb(178, 0xFF, 0xFF, 0xFF);
            binPackCard->Location = Point(22, 238);
            binPackCard->Size = Drawing::Size(836, 110);

            binPackTitle = MakeCardTitle(L"✿  单个 BIN 重封");
            binPackTitle->Location = Point(20, 14);

            manifestComboBox = gcnew ComboBox();
            StyleComboBox(manifestComboBox);
            manifestComboBox->FormattingEnabled = true;
            manifestComboBox->Location = Point(98, 44);
            manifestComboBox->Size = Drawing::Size(588, 25);

            refreshManifestButton = MakeGhostButton(L"刷新");
            refreshManifestButton->Location = Point(696, 42);
            refreshManifestButton->Size = Drawing::Size(68, 28);
            refreshManifestButton->Click += gcnew EventHandler(this, &MainForm::OnRefreshManifestClicked);

            browseManifestButton = MakeGhostButton(L"浏览");
            browseManifestButton->Location = Point(768, 42);
            browseManifestButton->Size = Drawing::Size(50, 28);
            browseManifestButton->Click += gcnew EventHandler(this, &MainForm::OnBrowseManifestClicked);

            outputBinTextBox = gcnew TextBox();
            StyleTextBox(outputBinTextBox);
            outputBinTextBox->Location = Point(98, 76);
            outputBinTextBox->Size = Drawing::Size(420, 24);

            browseOutputBinButton = MakeGhostButton(L"另存");
            browseOutputBinButton->Location = Point(528, 74);
            browseOutputBinButton->Size = Drawing::Size(58, 28);
            browseOutputBinButton->Click += gcnew EventHandler(this, &MainForm::OnBrowseOutputBinClicked);

            autoRebuildImagesCheckBox = gcnew CheckBox();
            StyleCheckBox(autoRebuildImagesCheckBox);
            autoRebuildImagesCheckBox->Location = Point(602, 78);
            autoRebuildImagesCheckBox->Text = L"自动回写 PNG";
            autoRebuildImagesCheckBox->Checked = true;

            repackBinButton = MakePrimaryButton(L"重封 BIN  ✿");
            repackBinButton->Location = Point(708, 74);
            repackBinButton->Size = Drawing::Size(110, 30);
            repackBinButton->Click += gcnew EventHandler(this, &MainForm::OnRepackBinClicked);

            binPackCard->Controls->Add(binPackTitle);
            binPackCard->Controls->Add(MakeFieldLabel(L"资源目录", 22, 48));
            binPackCard->Controls->Add(manifestComboBox);
            binPackCard->Controls->Add(refreshManifestButton);
            binPackCard->Controls->Add(browseManifestButton);
            binPackCard->Controls->Add(MakeFieldLabel(L"输出 BIN", 22, 80));
            binPackCard->Controls->Add(outputBinTextBox);
            binPackCard->Controls->Add(browseOutputBinButton);
            binPackCard->Controls->Add(autoRebuildImagesCheckBox);
            binPackCard->Controls->Add(repackBinButton);
        }

        void BuildPatchPackCard()
        {
            patchPackCard = gcnew RoundedCard();
            patchPackCard->FillColor = Color::FromArgb(178, 0xFF, 0xFF, 0xFF);
            patchPackCard->Location = Point(22, 358);
            patchPackCard->Size = Drawing::Size(836, 74);

            patchPackTitle = MakeCardTitle(L"✿  一键构建补丁");
            patchPackTitle->Location = Point(20, 14);

            patchOutputDirTextBox = gcnew TextBox();
            StyleTextBox(patchOutputDirTextBox);
            patchOutputDirTextBox->Location = Point(178, 40);
            patchOutputDirTextBox->Size = Drawing::Size(230, 24);

            browsePatchOutputDirButton = MakeGhostButton(L"浏览");
            browsePatchOutputDirButton->Location = Point(416, 38);
            browsePatchOutputDirButton->Size = Drawing::Size(58, 28);
            browsePatchOutputDirButton->Click += gcnew EventHandler(this, &MainForm::OnBrowsePatchOutputDirClicked);

            patchAutoRebuildCheckBox = gcnew CheckBox();
            StyleCheckBox(patchAutoRebuildCheckBox);
            patchAutoRebuildCheckBox->Location = Point(486, 44);
            patchAutoRebuildCheckBox->Text = L"自动回写 PNG";
            patchAutoRebuildCheckBox->Checked = true;

            includeUnchangedCheckBox = gcnew CheckBox();
            StyleCheckBox(includeUnchangedCheckBox);
            includeUnchangedCheckBox->Location = Point(602, 44);
            includeUnchangedCheckBox->Text = L"包含未改动项";

            buildPatchButton = MakeSecondaryButton(L"一键构建  ♡");
            buildPatchButton->Location = Point(708, 38);
            buildPatchButton->Size = Drawing::Size(110, 30);
            buildPatchButton->Click += gcnew EventHandler(this, &MainForm::OnBuildPatchClicked);

            patchPackCard->Controls->Add(patchPackTitle);
            patchPackCard->Controls->Add(MakeFieldLabel(L"输出目录", 116, 44));
            patchPackCard->Controls->Add(patchOutputDirTextBox);
            patchPackCard->Controls->Add(browsePatchOutputDirButton);
            patchPackCard->Controls->Add(patchAutoRebuildCheckBox);
            patchPackCard->Controls->Add(includeUnchangedCheckBox);
            patchPackCard->Controls->Add(buildPatchButton);
        }

        void BuildStatusBar()
        {
            statusLabel = gcnew Label();
            statusLabel->AutoSize = false;
            statusLabel->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 10.0f, FontStyle::Regular);
            statusLabel->ForeColor = Palette::TextPrimary;
            statusLabel->BackColor = Color::Transparent;
            statusLabel->Location = Point(22, 572);
            statusLabel->Size = Drawing::Size(580, 24);

            openResultButton = MakePrimaryButton(L"打开结果");
            openResultButton->Location = Point(680, 568);
            openResultButton->Size = Drawing::Size(108, 32);
            openResultButton->Click += gcnew EventHandler(this, &MainForm::OnOpenResultClicked);

            resetStateButton = MakeGhostButton(L"重置状态");
            resetStateButton->Location = Point(794, 568);
            resetStateButton->Size = Drawing::Size(108, 32);
            resetStateButton->Click += gcnew EventHandler(this, &MainForm::OnResetStateClicked);

            progressBar = gcnew ProgressBar();
            progressBar->Location = Point(22, 608);
            progressBar->Size = Drawing::Size(880, 18);
            progressBar->Style = ProgressBarStyle::Continuous;

            currentLogTitleLabel = gcnew Label();
            currentLogTitleLabel->AutoSize = false;
            currentLogTitleLabel->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 9.5f, FontStyle::Bold);
            currentLogTitleLabel->ForeColor = Palette::Accent;
            currentLogTitleLabel->BackColor = Color::Transparent;
            currentLogTitleLabel->Location = Point(22, 638);
            currentLogTitleLabel->Size = Drawing::Size(120, 22);
            currentLogTitleLabel->Text = L"当前步骤 ♡";

            currentLogBox = gcnew TextBox();
            currentLogBox->BackColor = Color::FromArgb(0xFF, 0xFA, 0xFC);
            currentLogBox->ForeColor = Palette::TextPrimary;
            currentLogBox->BorderStyle = BorderStyle::FixedSingle;
            currentLogBox->Font = gcnew ::System::Drawing::Font(L"Microsoft YaHei UI", 10.0f, FontStyle::Regular);
            currentLogBox->Location = Point(146, 636);
            currentLogBox->ReadOnly = true;
            currentLogBox->ShortcutsEnabled = false;
            currentLogBox->Size = Drawing::Size(756, 27);
        }

        // 路径默认值与解析
        String ^ GetWorkspaceRoot()
        {
            return Directory::GetCurrentDirectory();
        }

        String ^ GetDefaultUnpackFullPath()
        {
            return Path::GetFullPath(gcnew String(PackCppGetDefaultUnpackRoot()));
        }

        String ^ GetDefaultTextOutputFullPath()
        {
            return Path::GetFullPath(gcnew String(PackCppGetDefaultTextOutputDir()));
        }

        String ^ GetPreferredHcbPath()
        {
            array<String ^> ^ candidates = gcnew array<String ^>{
                Path::Combine(GetWorkspaceRoot(), L"AstralAirFinale.hcb"),
                Path::Combine(GetWorkspaceRoot(), L"output.hcb"),
            };
            for each (String ^ candidate in candidates)
            {
                if (File::Exists(candidate))
                {
                    return candidate;
                }
            }
            return candidates[0];
        }

        String ^ GetPreferredTranslationPath()
        {
            String ^ textRoot = GetDefaultTextOutputFullPath();
            String ^ jsonlPath = Path::Combine(textRoot, L"lines.jsonl");
            String ^ legacyPath = Path::Combine(textRoot, L"output.txt");
            if (File::Exists(jsonlPath))
            {
                return jsonlPath;
            }
            if (File::Exists(legacyPath))
            {
                return legacyPath;
            }
            return jsonlPath;
        }

        String ^ GetDefaultOutputHcbPath()
        {
            return Path::Combine(GetWorkspaceRoot(), L"output.hcb");
        }

        String ^ GetDefaultPatchOutputDir()
        {
            return Path::Combine(GetWorkspaceRoot(), L"patch_build");
        }

        String ^ ResolveOptionalPath(String ^ value)
        {
            if (String::IsNullOrWhiteSpace(value))
            {
                return String::Empty;
            }
            return Path::GetFullPath(value);
        }

        String ^ GetTranslationFormatArgument()
        {
            String ^ selected = dynamic_cast<String ^>(translationFormatComboBox->SelectedItem);
            if (String::IsNullOrWhiteSpace(selected) || selected == L"auto")
            {
                return String::Empty;
            }
            return selected;
        }

        String ^ DetectFormatFromPath(String ^ filePath)
        {
            if (String::IsNullOrWhiteSpace(filePath))
            {
                return String::Empty;
            }
            return Path::GetExtension(filePath)->Equals(L".jsonl", StringComparison::OrdinalIgnoreCase) ? L"jsonl" : L"legacy";
        }

        void RefreshManifestChoices()
        {
            String ^ previousSelection = manifestComboBox->Text;
            manifestComboBox->Items->Clear();

            String ^ unpackRoot = GetDefaultUnpackFullPath();
            if (Directory::Exists(unpackRoot))
            {
                array<String ^> ^ directories = Directory::GetDirectories(unpackRoot, L"extracted_*", SearchOption::TopDirectoryOnly);
                Array::Sort(directories, StringComparer::OrdinalIgnoreCase);
                for each (String ^ directoryPath in directories)
                {
                    if (File::Exists(Path::Combine(directoryPath, L"manifest.json")))
                    {
                        manifestComboBox->Items->Add(directoryPath);
                    }
                }
            }

            if (!String::IsNullOrWhiteSpace(previousSelection))
            {
                manifestComboBox->Text = previousSelection;
            }
            else if (manifestComboBox->Items->Count > 0)
            {
                manifestComboBox->SelectedIndex = 0;
            }
        }

        void RefreshPackDefaults(bool overwriteAll)
        {
            if (overwriteAll || String::IsNullOrWhiteSpace(sourceHcbTextBox->Text) || !File::Exists(sourceHcbTextBox->Text))
            {
                sourceHcbTextBox->Text = GetPreferredHcbPath();
            }
            if (overwriteAll || String::IsNullOrWhiteSpace(translationFileTextBox->Text) || !File::Exists(translationFileTextBox->Text))
            {
                translationFileTextBox->Text = GetPreferredTranslationPath();
            }
            if (overwriteAll || String::IsNullOrWhiteSpace(outputHcbTextBox->Text))
            {
                outputHcbTextBox->Text = GetDefaultOutputHcbPath();
            }
            if (overwriteAll || String::IsNullOrWhiteSpace(patchOutputDirTextBox->Text))
            {
                patchOutputDirTextBox->Text = GetDefaultPatchOutputDir();
            }
            if (translationFormatComboBox->SelectedIndex < 0)
            {
                translationFormatComboBox->SelectedIndex = 0;
            }
            if (textEncodingComboBox->SelectedIndex < 0)
            {
                textEncodingComboBox->SelectedIndex = 0;
            }
            RefreshManifestChoices();
        }

        // 状态栏与忙碌标志
        void SetCurrentLog(String ^ message)
        {
            currentLogBox->Text = String::IsNullOrWhiteSpace(message) ? L"等待任务开始。" : message;
            currentLogBox->SelectionStart = 0;
            currentLogBox->SelectionLength = 0;
        }

        void SetActionEnabledState(bool enabled)
        {
            extractTabButton->Enabled = enabled;
            packTabButton->Enabled = enabled;
            extractPanel->Enabled = enabled;
            packPanel->Enabled = enabled;
            openResultButton->Enabled = enabled && !String::IsNullOrWhiteSpace(lastOpenTarget);
            resetStateButton->Enabled = enabled;
        }

        void SetLastOpenTarget(String ^ path)
        {
            lastOpenTarget = ResolveOptionalPath(path);
            openResultButton->Enabled = !worker->IsBusy && !String::IsNullOrWhiteSpace(lastOpenTarget);
        }

        void SetIdleState(String ^ statusText, String ^ logText)
        {
            UseWaitCursor = false;
            busyStatusPrefix = String::Empty;
            SetActionEnabledState(true);
            statusLabel->Text = statusText;
            progressBar->Style = ProgressBarStyle::Continuous;
            progressBar->Value = 0;
            SetCurrentLog(logText);
        }

        void SetBusyState(String ^ statusText, String ^ logText)
        {
            UseWaitCursor = true;
            busyStatusPrefix = statusText;
            SetActionEnabledState(false);
            statusLabel->Text = statusText;
            progressBar->Style = ProgressBarStyle::Continuous;
            progressBar->Value = 0;
            SetCurrentLog(logText);
        }

        bool EnsureNotBusy()
        {
            if (worker->IsBusy)
            {
                statusLabel->Text = L"当前已有任务在运行，请等待完成。";
                return false;
            }
            return true;
        }

        void OpenPathInExplorer(String ^ path)
        {
            if (String::IsNullOrWhiteSpace(path))
            {
                return;
            }
            String ^ fullPath = Path::GetFullPath(path);
            if (File::Exists(fullPath))
            {
                Process::Start(L"explorer.exe", L"/select,\"" + fullPath + L"\"");
                return;
            }
            if (!Directory::Exists(fullPath))
            {
                Directory::CreateDirectory(fullPath);
            }
            Process::Start(L"explorer.exe", fullPath);
        }

        int CountSupportedFiles(array<String ^> ^ files)
        {
            if (files == nullptr)
            {
                return 0;
            }
            int count = 0;
            for each (String ^ filePath in files)
            {
                String ^ extension = Path::GetExtension(filePath)->ToLowerInvariant();
                if (extension == L".hcb" || extension == L".bin")
                {
                    ++count;
                }
            }
            return count;
        }

        bool IsSupportedDrop(array<String ^> ^ files)
        {
            return CountSupportedFiles(files) > 0;
        }

        String ^ GetNativeProgressMessage()
        {
            std::vector<wchar_t> messageBuffer(4096, L'\0');
            if (PackCppCopyProgressMessage(messageBuffer.data(), static_cast<int>(messageBuffer.size())) == 0)
            {
                return String::Empty;
            }
            return gcnew String(messageBuffer.data());
        }

        IntPtr AllocNativeString(String ^ text)
        {
            if (String::IsNullOrWhiteSpace(text))
            {
                return IntPtr::Zero;
            }
            return Marshal::StringToHGlobalUni(text);
        }

        void FreeNativeString(IntPtr pointer)
        {
            if (pointer != IntPtr::Zero)
            {
                Marshal::FreeHGlobal(pointer);
            }
        }

        void StartWork(WorkRequest ^ request, String ^ statusText, String ^ logText)
        {
            PackCppResetProgressState();
            SetBusyState(statusText, logText);
            progressTimer->Start();
            worker->RunWorkerAsync(request);
        }

        // 后台任务执行
        bool ProcessExtractFile(String ^ filePath, String ^ % resultMessage, String ^ % outputPath)
        {
            pin_ptr<const wchar_t> pinnedFilePath = PtrToStringChars(filePath);
            const int kindValue = PackCppDetectFileKind(pinnedFilePath);
            String ^ fileName = Path::GetFileName(filePath);
            if (kindValue == static_cast<int>(packcpp::DroppedFileKind::Unsupported))
            {
                resultMessage = L"跳过不支持的文件: " + fileName;
                outputPath = String::Empty;
                return false;
            }

            PackCppResetProgressState();
            std::vector<wchar_t> outputBuffer(32768, L'\0');
            const int ok = PackCppExtractDroppedFile(pinnedFilePath, outputBuffer.data(), static_cast<int>(outputBuffer.size()));
            if (ok == 0)
            {
                resultMessage = L"失败: " + fileName + L" -> " + gcnew String(PackCppGetLastErrorMessage());
                outputPath = String::Empty;
                return false;
            }

            outputPath = gcnew String(outputBuffer.data());
            String ^ kindText = kindValue == static_cast<int>(packcpp::DroppedFileKind::Hcb) ? L"文本" : L"资源";
            resultMessage = L"完成: [" + kindText + L"] " + fileName + L" -> " + outputPath;
            return true;
        }

        void ExecuteExtractRequest(WorkRequest ^ request, WorkResult ^ result)
        {
            String ^ lastOutputPath = GetDefaultUnpackFullPath();
            for each (String ^ filePath in request->files)
            {
                String ^ extension = Path::GetExtension(filePath)->ToLowerInvariant();
                if (extension != L".hcb" && extension != L".bin")
                {
                    continue;
                }

                ++result->handledCount;
                String ^ itemMessage = String::Empty;
                String ^ itemOutputPath = String::Empty;
                if (ProcessExtractFile(filePath, itemMessage, itemOutputPath))
                {
                    ++result->successCount;
                    if (!String::IsNullOrWhiteSpace(itemOutputPath))
                    {
                        lastOutputPath = itemOutputPath;
                    }
                }
                else
                {
                    ++result->failureCount;
                }
                result->detail = itemMessage;
            }

            result->primaryPath = lastOutputPath;
            if (result->failureCount == 0)
            {
                result->summary = L"解包完成，共处理 " + result->handledCount.ToString() + L" 个文件。";
            }
            else
            {
                result->summary = L"解包完成，成功 " + result->successCount.ToString() + L" 个，失败 " + result->failureCount.ToString() + L" 个。";
            }
        }

        void ExecuteRepackTextRequest(WorkRequest ^ request, WorkResult ^ result)
        {
            IntPtr hcbPointer = AllocNativeString(request->sourceHcbPath);
            IntPtr translationPointer = AllocNativeString(request->translationPath);
            IntPtr outputPointer = AllocNativeString(request->outputPath);
            IntPtr formatPointer = AllocNativeString(request->translationFormat);
            IntPtr encodingPointer = AllocNativeString(request->textEncoding);

            try
            {
                std::vector<wchar_t> outputBuffer(32768, L'\0');
                const int ok = PackCppRepackTextFile(
                    static_cast<const wchar_t *>(hcbPointer.ToPointer()),
                    static_cast<const wchar_t *>(translationPointer.ToPointer()),
                    static_cast<const wchar_t *>(outputPointer.ToPointer()),
                    static_cast<const wchar_t *>(formatPointer.ToPointer()),
                    static_cast<const wchar_t *>(encodingPointer.ToPointer()),
                    outputBuffer.data(),
                    static_cast<int>(outputBuffer.size()));
                if (ok == 0)
                {
                    throw gcnew InvalidOperationException(gcnew String(PackCppGetLastErrorMessage()));
                }

                result->handledCount = 1;
                result->successCount = 1;
                result->primaryPath = gcnew String(outputBuffer.data());
                result->summary = L"文本封包完成 ♡";
                result->detail = L"输出 HCB: " + result->primaryPath;
            }
            finally
            {
                FreeNativeString(hcbPointer);
                FreeNativeString(translationPointer);
                FreeNativeString(outputPointer);
                FreeNativeString(formatPointer);
                FreeNativeString(encodingPointer);
            }
        }

        void ExecuteRepackBinRequest(WorkRequest ^ request, WorkResult ^ result)
        {
            IntPtr manifestPointer = AllocNativeString(request->manifestOrDir);
            IntPtr outputPointer = AllocNativeString(request->outputPath);

            try
            {
                std::vector<wchar_t> outputBuffer(32768, L'\0');
                std::vector<wchar_t> reportBuffer(32768, L'\0');
                const int ok = PackCppRepackBinFile(
                    static_cast<const wchar_t *>(manifestPointer.ToPointer()),
                    static_cast<const wchar_t *>(outputPointer.ToPointer()),
                    request->autoRebuildImages ? 1 : 0,
                    outputBuffer.data(),
                    static_cast<int>(outputBuffer.size()),
                    reportBuffer.data(),
                    static_cast<int>(reportBuffer.size()));
                if (ok == 0)
                {
                    throw gcnew InvalidOperationException(gcnew String(PackCppGetLastErrorMessage()));
                }

                result->handledCount = 1;
                result->successCount = 1;
                result->primaryPath = gcnew String(outputBuffer.data());
                result->secondaryPath = gcnew String(reportBuffer.data());
                result->summary = L"资源封包完成 ✿";
                result->detail = L"输出 BIN: " + result->primaryPath + L" | 报告: " + result->secondaryPath;
            }
            finally
            {
                FreeNativeString(manifestPointer);
                FreeNativeString(outputPointer);
            }
        }

        void ExecuteBuildPatchRequest(WorkRequest ^ request, WorkResult ^ result)
        {
            IntPtr inputRootPointer = AllocNativeString(request->patchInputRoot);
            IntPtr outputDirPointer = AllocNativeString(request->patchOutputDir);
            IntPtr translationPointer = AllocNativeString(request->translationPath);
            IntPtr formatPointer = AllocNativeString(request->translationFormat);
            IntPtr hcbPointer = AllocNativeString(request->sourceHcbPath);
            IntPtr textOutputPointer = AllocNativeString(request->outputPath);
            IntPtr encodingPointer = AllocNativeString(request->textEncoding);

            try
            {
                std::vector<wchar_t> reportBuffer(32768, L'\0');
                const int ok = PackCppBuildPatchBundle(
                    static_cast<const wchar_t *>(inputRootPointer.ToPointer()),
                    static_cast<const wchar_t *>(outputDirPointer.ToPointer()),
                    request->autoRebuildImages ? 1 : 0,
                    request->includeUnchanged ? 1 : 0,
                    static_cast<const wchar_t *>(translationPointer.ToPointer()),
                    static_cast<const wchar_t *>(formatPointer.ToPointer()),
                    static_cast<const wchar_t *>(hcbPointer.ToPointer()),
                    static_cast<const wchar_t *>(textOutputPointer.ToPointer()),
                    static_cast<const wchar_t *>(encodingPointer.ToPointer()),
                    reportBuffer.data(),
                    static_cast<int>(reportBuffer.size()));
                if (ok == 0)
                {
                    throw gcnew InvalidOperationException(gcnew String(PackCppGetLastErrorMessage()));
                }

                result->handledCount = 1;
                result->successCount = 1;
                result->primaryPath = ResolveOptionalPath(request->patchOutputDir);
                result->secondaryPath = gcnew String(reportBuffer.data());
                result->summary = L"补丁构建完成 ♡";
                result->detail = L"补丁目录: " + result->primaryPath + L" | 报告: " + result->secondaryPath;
            }
            finally
            {
                FreeNativeString(inputRootPointer);
                FreeNativeString(outputDirPointer);
                FreeNativeString(translationPointer);
                FreeNativeString(formatPointer);
                FreeNativeString(hcbPointer);
                FreeNativeString(textOutputPointer);
                FreeNativeString(encodingPointer);
            }
        }

        // 事件回调
        void OnDragEnter(Object ^ sender, DragEventArgs ^ e)
        {
            if (worker->IsBusy)
            {
                e->Effect = DragDropEffects::None;
                return;
            }
            if (!e->Data->GetDataPresent(DataFormats::FileDrop))
            {
                e->Effect = DragDropEffects::None;
                return;
            }
            array<String ^> ^ files = safe_cast<array<String ^> ^>(e->Data->GetData(DataFormats::FileDrop));
            e->Effect = IsSupportedDrop(files) ? DragDropEffects::Copy : DragDropEffects::None;
        }

        void OnDragDrop(Object ^ sender, DragEventArgs ^ e)
        {
            if (!EnsureNotBusy())
            {
                return;
            }
            if (!e->Data->GetDataPresent(DataFormats::FileDrop))
            {
                return;
            }
            array<String ^> ^ files = safe_cast<array<String ^> ^>(e->Data->GetData(DataFormats::FileDrop));
            if (!IsSupportedDrop(files))
            {
                SetIdleState(L"没有检测到可处理的 .hcb 或 .bin 文件。", L"等待拖入 .hcb 或 .bin 文件。");
                return;
            }

            WorkRequest ^ request = gcnew WorkRequest();
            request->kind = UiWorkKind::Extract;
            request->files = files;
            const int supportedCount = CountSupportedFiles(files);
            StartWork(request, L"正在解包 " + supportedCount.ToString() + L" 个文件...", L"准备开始解包任务...");
        }

        void OnRefreshDefaultsClicked(Object ^ sender, EventArgs ^ e)
        {
            if (!EnsureNotBusy())
            {
                return;
            }
            RefreshPackDefaults(true);
            SetIdleState(L"已刷新默认路径 ♡", L"Pack 页签的默认路径与目录列表已更新。");
        }

        void OnBrowseSourceHcbClicked(Object ^ sender, EventArgs ^ e)
        {
            OpenFileDialog ^ dialog = gcnew OpenFileDialog();
            dialog->Filter = L"HCB 文件|*.hcb|所有文件|*.*";
            dialog->Title = L"选择源 HCB 文件";
            if (dialog->ShowDialog(this) == System::Windows::Forms::DialogResult::OK)
            {
                sourceHcbTextBox->Text = dialog->FileName;
            }
        }

        void OnBrowseTranslationClicked(Object ^ sender, EventArgs ^ e)
        {
            OpenFileDialog ^ dialog = gcnew OpenFileDialog();
            dialog->Filter = L"翻译文件|*.jsonl;*.txt|JSONL 文件|*.jsonl|文本文件|*.txt|所有文件|*.*";
            dialog->Title = L"选择翻译文件";
            if (dialog->ShowDialog(this) == System::Windows::Forms::DialogResult::OK)
            {
                translationFileTextBox->Text = dialog->FileName;
            }
        }

        void OnBrowseOutputHcbClicked(Object ^ sender, EventArgs ^ e)
        {
            SaveFileDialog ^ dialog = gcnew SaveFileDialog();
            dialog->Filter = L"HCB 文件|*.hcb|所有文件|*.*";
            dialog->Title = L"选择输出 HCB 路径";
            dialog->FileName = Path::GetFileName(outputHcbTextBox->Text);
            if (dialog->ShowDialog(this) == System::Windows::Forms::DialogResult::OK)
            {
                outputHcbTextBox->Text = dialog->FileName;
            }
        }

        void OnRefreshManifestClicked(Object ^ sender, EventArgs ^ e)
        {
            RefreshManifestChoices();
            SetIdleState(L"已刷新 extracted_xxx 列表 ✿", L"单个 BIN 重封列表已更新。");
        }

        void OnBrowseManifestClicked(Object ^ sender, EventArgs ^ e)
        {
            FolderBrowserDialog ^ dialog = gcnew FolderBrowserDialog();
            dialog->Description = L"选择 extracted_xxx 目录";
            dialog->SelectedPath = Directory::Exists(GetDefaultUnpackFullPath()) ? GetDefaultUnpackFullPath() : GetWorkspaceRoot();
            if (dialog->ShowDialog(this) == System::Windows::Forms::DialogResult::OK)
            {
                manifestComboBox->Text = dialog->SelectedPath;
            }
        }

        void OnBrowseOutputBinClicked(Object ^ sender, EventArgs ^ e)
        {
            SaveFileDialog ^ dialog = gcnew SaveFileDialog();
            dialog->Filter = L"BIN 文件|*.bin|所有文件|*.*";
            dialog->Title = L"选择输出 BIN 路径";
            dialog->FileName = Path::GetFileName(outputBinTextBox->Text);
            if (dialog->ShowDialog(this) == System::Windows::Forms::DialogResult::OK)
            {
                outputBinTextBox->Text = dialog->FileName;
            }
        }

        void OnBrowsePatchOutputDirClicked(Object ^ sender, EventArgs ^ e)
        {
            FolderBrowserDialog ^ dialog = gcnew FolderBrowserDialog();
            dialog->Description = L"选择补丁输出目录";
            dialog->SelectedPath = Directory::Exists(patchOutputDirTextBox->Text) ? patchOutputDirTextBox->Text : GetWorkspaceRoot();
            if (dialog->ShowDialog(this) == System::Windows::Forms::DialogResult::OK)
            {
                patchOutputDirTextBox->Text = dialog->SelectedPath;
            }
        }

        void OnRepackTextClicked(Object ^ sender, EventArgs ^ e)
        {
            if (!EnsureNotBusy())
            {
                return;
            }

            String ^ translationPath = ResolveOptionalPath(translationFileTextBox->Text);
            if (String::IsNullOrWhiteSpace(translationPath) || !File::Exists(translationPath))
            {
                SetIdleState(L"无法开始文本封包。", L"请先提供存在的翻译文件。默认位置通常是 unpack\\text\\lines.jsonl 或 output.txt。");
                return;
            }

            String ^ sourceHcbPath = ResolveOptionalPath(sourceHcbTextBox->Text);
            if (String::IsNullOrWhiteSpace(sourceHcbPath) || !File::Exists(sourceHcbPath))
            {
                SetIdleState(L"无法开始文本封包。", L"请先提供存在的源 HCB 文件。");
                return;
            }

            WorkRequest ^ request = gcnew WorkRequest();
            request->kind = UiWorkKind::RepackText;
            request->sourceHcbPath = sourceHcbPath;
            request->translationPath = translationPath;
            request->outputPath = ResolveOptionalPath(outputHcbTextBox->Text);
            request->translationFormat = GetTranslationFormatArgument();
            request->textEncoding = dynamic_cast<String ^>(textEncodingComboBox->SelectedItem);
            StartWork(request, L"正在封包文本...", L"准备写回 output.hcb ...");
        }

        void OnRepackBinClicked(Object ^ sender, EventArgs ^ e)
        {
            if (!EnsureNotBusy())
            {
                return;
            }

            String ^ manifestOrDir = ResolveOptionalPath(manifestComboBox->Text);
            if (String::IsNullOrWhiteSpace(manifestOrDir) || (!Directory::Exists(manifestOrDir) && !File::Exists(manifestOrDir)))
            {
                SetIdleState(L"无法开始 BIN 封包。", L"请先选择一个 extracted_xxx 目录，或直接指定 manifest.json。");
                return;
            }

            WorkRequest ^ request = gcnew WorkRequest();
            request->kind = UiWorkKind::RepackBin;
            request->manifestOrDir = manifestOrDir;
            request->outputPath = ResolveOptionalPath(outputBinTextBox->Text);
            request->autoRebuildImages = autoRebuildImagesCheckBox->Checked;
            StartWork(request, L"正在封包单个 BIN...", L"准备写出新的资源容器...");
        }

        void OnBuildPatchClicked(Object ^ sender, EventArgs ^ e)
        {
            if (!EnsureNotBusy())
            {
                return;
            }

            WorkRequest ^ request = gcnew WorkRequest();
            request->kind = UiWorkKind::BuildPatch;
            request->patchInputRoot = GetDefaultUnpackFullPath();
            request->patchOutputDir = ResolveOptionalPath(patchOutputDirTextBox->Text);
            if (String::IsNullOrWhiteSpace(request->patchOutputDir))
            {
                request->patchOutputDir = GetDefaultPatchOutputDir();
            }
            request->autoRebuildImages = patchAutoRebuildCheckBox->Checked;
            request->includeUnchanged = includeUnchangedCheckBox->Checked;

            String ^ translationPath = ResolveOptionalPath(translationFileTextBox->Text);
            if (File::Exists(translationPath))
            {
                request->translationPath = translationPath;
                request->translationFormat = GetTranslationFormatArgument();
                if (String::IsNullOrWhiteSpace(request->translationFormat))
                {
                    request->translationFormat = DetectFormatFromPath(translationPath);
                }
            }

            String ^ sourceHcbPath = ResolveOptionalPath(sourceHcbTextBox->Text);
            if (File::Exists(sourceHcbPath))
            {
                request->sourceHcbPath = sourceHcbPath;
            }

            String ^ outputHcbPath = ResolveOptionalPath(outputHcbTextBox->Text);
            if (!String::IsNullOrWhiteSpace(outputHcbPath))
            {
                request->outputPath = outputHcbPath;
            }

            request->textEncoding = dynamic_cast<String ^>(textEncodingComboBox->SelectedItem);
            StartWork(request, L"正在构建补丁...", L"准备扫描 unpack 目录并生成补丁产物...");
        }

        void OnProgressTimerTick(Object ^ sender, EventArgs ^ e)
        {
            if (!worker->IsBusy)
            {
                return;
            }
            int progressValue = Math::Max(0, Math::Min(100, PackCppGetProgressPercent()));
            progressBar->Value = progressValue;
            statusLabel->Text = busyStatusPrefix + L" " + progressValue.ToString() + L"%";
            String ^ nativeMessage = GetNativeProgressMessage();
            if (!String::IsNullOrWhiteSpace(nativeMessage))
            {
                SetCurrentLog(nativeMessage);
            }
        }

        void OnWorkerDoWork(Object ^ sender, DoWorkEventArgs ^ e)
        {
            WorkRequest ^ request = safe_cast<WorkRequest ^>(e->Argument);
            WorkResult ^ result = gcnew WorkResult();

            try
            {
                switch (request->kind)
                {
                case UiWorkKind::Extract:
                    ExecuteExtractRequest(request, result);
                    break;
                case UiWorkKind::RepackText:
                    ExecuteRepackTextRequest(request, result);
                    break;
                case UiWorkKind::RepackBin:
                    ExecuteRepackBinRequest(request, result);
                    break;
                case UiWorkKind::BuildPatch:
                    ExecuteBuildPatchRequest(request, result);
                    break;
                default:
                    throw gcnew InvalidOperationException(L"未知任务类型。");
                }
            }
            catch (Exception ^ ex)
            {
                result->failureCount = result->handledCount > 0 ? result->handledCount : 1;
                result->summary = L"任务失败。";
                result->detail = ex->Message;
                throw;
            }

            e->Result = result;
        }

        void OnWorkerCompleted(Object ^ sender, RunWorkerCompletedEventArgs ^ e)
        {
            progressTimer->Stop();

            if (e->Error != nullptr)
            {
                SetLastOpenTarget(GetDefaultUnpackFullPath());
                SetIdleState(L"任务失败。", e->Error->Message);
                return;
            }

            WorkResult ^ result = safe_cast<WorkResult ^>(e->Result);
            if (result == nullptr)
            {
                SetLastOpenTarget(GetDefaultUnpackFullPath());
                SetIdleState(L"没有产生任何结果。", L"请重新执行操作。");
                return;
            }

            progressBar->Value = 100;
            statusLabel->Text = result->summary;
            UseWaitCursor = false;
            busyStatusPrefix = String::Empty;
            SetActionEnabledState(true);
            SetCurrentLog(result->detail);

            if (!String::IsNullOrWhiteSpace(result->primaryPath))
            {
                SetLastOpenTarget(result->primaryPath);
            }
            else
            {
                SetLastOpenTarget(GetDefaultUnpackFullPath());
            }

            RefreshManifestChoices();
        }

        void OnOpenResultClicked(Object ^ sender, EventArgs ^ e)
        {
            if (!String::IsNullOrWhiteSpace(lastOpenTarget))
            {
                OpenPathInExplorer(lastOpenTarget);
                return;
            }
            OpenPathInExplorer(GetDefaultUnpackFullPath());
        }

        void OnResetStateClicked(Object ^ sender, EventArgs ^ e)
        {
            if (!EnsureNotBusy())
            {
                return;
            }
            PackCppResetProgressState();
            RefreshManifestChoices();
            SetLastOpenTarget(GetDefaultUnpackFullPath());
            SetIdleState(L"状态已重置 ♡", L"可以在 Extract 页签继续解包，或在 Pack 页签执行封包。");
        }
    };

} // namespace fvpyuki
