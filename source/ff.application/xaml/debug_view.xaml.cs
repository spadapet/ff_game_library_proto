﻿using System.Windows.Controls;

namespace ff
{
    public partial class debug_view : UserControl
    {
        public debug_view_model view_model { get; } = new debug_view_model();

        public debug_view()
        {
            this.InitializeComponent();
        }
    }
}
