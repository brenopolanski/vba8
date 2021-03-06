﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Navigation;
using Microsoft.Phone.Controls;
using Microsoft.Phone.Shell;
using PhoneDirect3DXamlAppInterop.Resources;
using Microsoft.Phone.Tasks;

namespace PhoneDirect3DXamlAppInterop
{
    public partial class LicensePage : PhoneApplicationPage
    {
        public LicensePage()
        {
            InitializeComponent();

#if GBC
            this.pivot.Title = AppResources.ApplicationTitle2;
#endif
        }

        private void sourceButton_Click(object sender, RoutedEventArgs e)
        {
            WebBrowserTask wbt = new WebBrowserTask();
            wbt.URL = AppResources.SourceLink;
            wbt.Show();
        }

        private void gplButton_Click(object sender, RoutedEventArgs e)
        {
            WebBrowserTask wbt = new WebBrowserTask();
            wbt.URL = AppResources.GPLv3;
            wbt.Show();
        }
    }
}