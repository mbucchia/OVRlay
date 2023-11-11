
namespace WindowsFormsApp
{
    partial class OverlayForm
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.components = new System.ComponentModel.Container();
            this.tableLayoutPanel1 = new System.Windows.Forms.TableLayoutPanel();
            this.tableLayoutPanel2 = new System.Windows.Forms.TableLayoutPanel();
            this.import = new System.Windows.Forms.Button();
            this.remove = new System.Windows.Forms.Button();
            this.flowLayoutPanel1 = new System.Windows.Forms.FlowLayoutPanel();
            this.opacityLabel = new System.Windows.Forms.Label();
            this.opacity = new System.Windows.Forms.TrackBar();
            this.placementLabel = new System.Windows.Forms.Label();
            this.placement = new System.Windows.Forms.ComboBox();
            this.freeze = new System.Windows.Forms.CheckBox();
            this.allowInteractions = new System.Windows.Forms.CheckBox();
            this.availableWindows = new System.Windows.Forms.ListBox();
            this.importedWindows = new System.Windows.Forms.ListBox();
            this.refresh = new System.Windows.Forms.Timer(this.components);
            this.tableLayoutPanel1.SuspendLayout();
            this.tableLayoutPanel2.SuspendLayout();
            this.flowLayoutPanel1.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this.opacity)).BeginInit();
            this.SuspendLayout();
            // 
            // tableLayoutPanel1
            // 
            this.tableLayoutPanel1.ColumnCount = 1;
            this.tableLayoutPanel1.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 100F));
            this.tableLayoutPanel1.Controls.Add(this.tableLayoutPanel2, 0, 1);
            this.tableLayoutPanel1.Controls.Add(this.flowLayoutPanel1, 0, 3);
            this.tableLayoutPanel1.Controls.Add(this.availableWindows, 0, 0);
            this.tableLayoutPanel1.Controls.Add(this.importedWindows, 0, 2);
            this.tableLayoutPanel1.Dock = System.Windows.Forms.DockStyle.Fill;
            this.tableLayoutPanel1.Location = new System.Drawing.Point(0, 0);
            this.tableLayoutPanel1.Name = "tableLayoutPanel1";
            this.tableLayoutPanel1.RowCount = 4;
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Percent, 70F));
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Absolute, 54F));
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Percent, 30F));
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Absolute, 110F));
            this.tableLayoutPanel1.Size = new System.Drawing.Size(365, 402);
            this.tableLayoutPanel1.TabIndex = 0;
            // 
            // tableLayoutPanel2
            // 
            this.tableLayoutPanel2.ColumnCount = 2;
            this.tableLayoutPanel2.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 50F));
            this.tableLayoutPanel2.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 50F));
            this.tableLayoutPanel2.Controls.Add(this.import, 0, 0);
            this.tableLayoutPanel2.Controls.Add(this.remove, 1, 0);
            this.tableLayoutPanel2.Dock = System.Windows.Forms.DockStyle.Fill;
            this.tableLayoutPanel2.Location = new System.Drawing.Point(3, 169);
            this.tableLayoutPanel2.Name = "tableLayoutPanel2";
            this.tableLayoutPanel2.RowCount = 1;
            this.tableLayoutPanel2.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Percent, 50F));
            this.tableLayoutPanel2.Size = new System.Drawing.Size(359, 48);
            this.tableLayoutPanel2.TabIndex = 1;
            // 
            // import
            // 
            this.import.Dock = System.Windows.Forms.DockStyle.Fill;
            this.import.Location = new System.Drawing.Point(3, 3);
            this.import.Name = "import";
            this.import.Size = new System.Drawing.Size(173, 42);
            this.import.TabIndex = 0;
            this.import.Text = "Import";
            this.import.UseVisualStyleBackColor = true;
            this.import.Click += new System.EventHandler(this.import_Click);
            // 
            // remove
            // 
            this.remove.Dock = System.Windows.Forms.DockStyle.Fill;
            this.remove.Location = new System.Drawing.Point(182, 3);
            this.remove.Name = "remove";
            this.remove.Size = new System.Drawing.Size(174, 42);
            this.remove.TabIndex = 1;
            this.remove.Text = "Remove";
            this.remove.UseVisualStyleBackColor = true;
            this.remove.Click += new System.EventHandler(this.remove_Click);
            // 
            // flowLayoutPanel1
            // 
            this.flowLayoutPanel1.Controls.Add(this.opacityLabel);
            this.flowLayoutPanel1.Controls.Add(this.opacity);
            this.flowLayoutPanel1.Controls.Add(this.placementLabel);
            this.flowLayoutPanel1.Controls.Add(this.placement);
            this.flowLayoutPanel1.Controls.Add(this.freeze);
            this.flowLayoutPanel1.Controls.Add(this.allowInteractions);
            this.flowLayoutPanel1.Dock = System.Windows.Forms.DockStyle.Fill;
            this.flowLayoutPanel1.Location = new System.Drawing.Point(3, 294);
            this.flowLayoutPanel1.Name = "flowLayoutPanel1";
            this.flowLayoutPanel1.Size = new System.Drawing.Size(359, 105);
            this.flowLayoutPanel1.TabIndex = 1;
            // 
            // opacityLabel
            // 
            this.opacityLabel.AutoSize = true;
            this.opacityLabel.Location = new System.Drawing.Point(3, 0);
            this.opacityLabel.Name = "opacityLabel";
            this.opacityLabel.Padding = new System.Windows.Forms.Padding(0, 6, 0, 0);
            this.opacityLabel.Size = new System.Drawing.Size(46, 19);
            this.opacityLabel.TabIndex = 1;
            this.opacityLabel.Text = "Opacity:";
            // 
            // opacity
            // 
            this.flowLayoutPanel1.SetFlowBreak(this.opacity, true);
            this.opacity.Location = new System.Drawing.Point(55, 3);
            this.opacity.Margin = new System.Windows.Forms.Padding(3, 3, 3, 0);
            this.opacity.Maximum = 100;
            this.opacity.Minimum = 1;
            this.opacity.Name = "opacity";
            this.opacity.Size = new System.Drawing.Size(104, 45);
            this.opacity.TabIndex = 2;
            this.opacity.TickFrequency = 10;
            this.opacity.Value = 100;
            this.opacity.Scroll += new System.EventHandler(this.opacity_Scroll);
            // 
            // placementLabel
            // 
            this.placementLabel.AutoSize = true;
            this.placementLabel.Location = new System.Drawing.Point(3, 48);
            this.placementLabel.Name = "placementLabel";
            this.placementLabel.Padding = new System.Windows.Forms.Padding(0, 3, 0, 0);
            this.placementLabel.Size = new System.Drawing.Size(60, 16);
            this.placementLabel.TabIndex = 3;
            this.placementLabel.Text = "Placement:";
            // 
            // placement
            // 
            this.placement.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            this.flowLayoutPanel1.SetFlowBreak(this.placement, true);
            this.placement.FormattingEnabled = true;
            this.placement.Items.AddRange(new object[] {
            "Relative to world",
            "Relative to head",
            "Left hand \"watch\"",
            "Left hand \"palm\"",
            "Right hand \"watch\"",
            "Right hand \"palm\""});
            this.placement.Location = new System.Drawing.Point(69, 48);
            this.placement.Margin = new System.Windows.Forms.Padding(3, 0, 3, 3);
            this.placement.Name = "placement";
            this.placement.Size = new System.Drawing.Size(121, 21);
            this.placement.TabIndex = 4;
            this.placement.SelectedIndexChanged += new System.EventHandler(this.placement_SelectedIndexChanged);
            // 
            // freeze
            // 
            this.freeze.AutoSize = true;
            this.freeze.Location = new System.Drawing.Point(3, 75);
            this.freeze.Name = "freeze";
            this.freeze.Padding = new System.Windows.Forms.Padding(0, 3, 0, 0);
            this.freeze.Size = new System.Drawing.Size(133, 20);
            this.freeze.TabIndex = 5;
            this.freeze.Text = "Freeze placement/size";
            this.freeze.UseVisualStyleBackColor = true;
            this.freeze.CheckedChanged += new System.EventHandler(this.freeze_CheckedChanged);
            // 
            // allowInteractions
            // 
            this.allowInteractions.AutoSize = true;
            this.allowInteractions.Location = new System.Drawing.Point(142, 75);
            this.allowInteractions.Name = "allowInteractions";
            this.allowInteractions.Padding = new System.Windows.Forms.Padding(0, 3, 0, 0);
            this.allowInteractions.Size = new System.Drawing.Size(108, 20);
            this.allowInteractions.TabIndex = 6;
            this.allowInteractions.Text = "Allow interactions";
            this.allowInteractions.UseVisualStyleBackColor = true;
            this.allowInteractions.CheckedChanged += new System.EventHandler(this.allowInteractions_CheckedChanged);
            // 
            // availableWindows
            // 
            this.availableWindows.Dock = System.Windows.Forms.DockStyle.Fill;
            this.availableWindows.FormattingEnabled = true;
            this.availableWindows.Location = new System.Drawing.Point(3, 3);
            this.availableWindows.Name = "availableWindows";
            this.availableWindows.Size = new System.Drawing.Size(359, 160);
            this.availableWindows.TabIndex = 1;
            this.availableWindows.SelectedIndexChanged += new System.EventHandler(this.availableWindows_SelectedIndexChanged);
            // 
            // importedWindows
            // 
            this.importedWindows.Dock = System.Windows.Forms.DockStyle.Fill;
            this.importedWindows.FormattingEnabled = true;
            this.importedWindows.Location = new System.Drawing.Point(3, 223);
            this.importedWindows.Name = "importedWindows";
            this.importedWindows.Size = new System.Drawing.Size(359, 65);
            this.importedWindows.TabIndex = 1;
            this.importedWindows.SelectedIndexChanged += new System.EventHandler(this.importedWindows_SelectedIndexChanged);
            // 
            // refresh
            // 
            this.refresh.Enabled = true;
            this.refresh.Interval = 5000;
            this.refresh.Tick += new System.EventHandler(this.refresh_Tick);
            // 
            // OverlayForm
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(365, 402);
            this.Controls.Add(this.tableLayoutPanel1);
            this.FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedSingle;
            this.Margin = new System.Windows.Forms.Padding(2);
            this.MaximizeBox = false;
            this.Name = "OverlayForm";
            this.Text = "Virtual Desktop Window Manager";
            this.tableLayoutPanel1.ResumeLayout(false);
            this.tableLayoutPanel2.ResumeLayout(false);
            this.flowLayoutPanel1.ResumeLayout(false);
            this.flowLayoutPanel1.PerformLayout();
            ((System.ComponentModel.ISupportInitialize)(this.opacity)).EndInit();
            this.ResumeLayout(false);

        }

        #endregion

        private System.Windows.Forms.TableLayoutPanel tableLayoutPanel1;
        private System.Windows.Forms.FlowLayoutPanel flowLayoutPanel1;
        private System.Windows.Forms.TableLayoutPanel tableLayoutPanel2;
        private System.Windows.Forms.Button import;
        private System.Windows.Forms.Button remove;
        private System.Windows.Forms.Label opacityLabel;
        private System.Windows.Forms.TrackBar opacity;
        private System.Windows.Forms.Label placementLabel;
        private System.Windows.Forms.ComboBox placement;
        private System.Windows.Forms.CheckBox freeze;
        private System.Windows.Forms.CheckBox allowInteractions;
        private System.Windows.Forms.ListBox availableWindows;
        private System.Windows.Forms.ListBox importedWindows;
        private System.Windows.Forms.Timer refresh;
    }
}

