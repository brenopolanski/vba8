﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Navigation;
using Microsoft.Phone.Controls;
using Microsoft.Phone.Shell;
using Microsoft.Live;
using Microsoft.Live.Controls;
using Windows.Storage;
using Windows.Storage.Streams;
using System.Threading.Tasks;
using PhoneDirect3DXamlAppInterop.Resources;
using PhoneDirect3DXamlAppInterop.Database;
using System.Windows.Media;
using Microsoft.Phone.Tasks;

namespace PhoneDirect3DXamlAppInterop
{
    public enum SkyDriveItemType
    {
        File,
        Folder,
        ROM,
        SRAM,
        Savestate
    }

    public class SkyDriveListItem
    {
        public String Name { get; set; }
        public String SkyDriveID { get; set; }
        public String ParentID { get; set; }
        public int FolderChildrenCount { get; set; }
        public SkyDriveItemType Type { get; set; }
        public bool Downloading { get; set; }
    }

    public partial class SkyDriveImportPage : PhoneApplicationPage
    {
        private LiveConnectSession session;
        List<List<SkyDriveListItem>> skydriveStack;
        private double labelHeight;
        int downloadsInProgress = 0;
        bool initialPageLoaded = false;

        public SkyDriveImportPage()
        {
            InitializeComponent();
            this.labelHeight = this.statusLabel.Height;
            this.session = PhoneApplicationService.Current.State["parameter"] as LiveConnectSession;
            PhoneApplicationService.Current.State.Remove("parameter");
            if (this.session == null)
            {
                throw new ArgumentException("Parameter passed to SkyDriveImportPage must be a LiveConnectSession.");
            }

            this.skydriveStack = new List<List<SkyDriveListItem>>();
            this.skydriveStack.Add(new List<SkyDriveListItem>());
            this.skydriveStack[0].Add(new SkyDriveListItem()
            {
                Name = "SkyDrive",
                SkyDriveID = "me/skydrive",
                Type = SkyDriveItemType.Folder,
                ParentID = ""
            });
            this.currentFolderBox.Text = this.skydriveStack[0][0].Name;

            this.BackKeyPress += SkyDriveImportPage_BackKeyPress;

#if GBC
            this.fileFormatLabel.Text = AppResources.SkyDriveFileFormatHint2;
            this.titleLabel.Text = AppResources.ApplicationTitle2;
#endif
        }

        async void skydriveList_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            SkyDriveListItem item = this.skydriveList.SelectedItem as SkyDriveListItem;
            if (item == null)
                return;

            ROMDatabase db = ROMDatabase.Current;
            if (App.IsTrial && (db.GetNumberOfROMs() + this.downloadsInProgress) >= 2)
            {
                this.skydriveList.SelectedItem = null;
                this.ShowBuyDialog();
                return;
            }

            try
            {
                LiveConnectClient client = new LiveConnectClient(this.session);
                if (item.Type == SkyDriveItemType.Folder)
                {
                    if (this.session != null)
                    {
                        this.skydriveList.ItemsSource = null;
                        this.currentFolderBox.Text = item.Name;
                        LiveOperationResult result = await client.GetAsync(item.SkyDriveID + "/files");
                        this.client_GetCompleted(result);
                    }
                }
                else if (item.Type == SkyDriveItemType.ROM)
                {
                    // Download
                    if (!item.Downloading)
                    {
                        try
                        {
                            this.downloadsInProgress++;
                            item.Downloading = true;
                            await this.DownloadFile(item, client);
                        }
                        catch (Exception ex)
                        {
                            MessageBox.Show(String.Format(AppResources.DownloadErrorText, item.Name, ex.Message), AppResources.ErrorCaption, MessageBoxButton.OK);
                        }
                        finally
                        {
                            this.downloadsInProgress--;
                        }
                    }
                    else
                    {
                        MessageBox.Show(AppResources.AlreadyDownloadingText, AppResources.ErrorCaption, MessageBoxButton.OK);
                    }
                }
                this.statusLabel.Height = 0;
            }
            catch (LiveConnectException)
            {
                this.statusLabel.Height = this.labelHeight;
                this.statusLabel.Text = AppResources.SkyDriveInternetLost;
            }
        }

        private async Task DownloadFile(SkyDriveListItem item, LiveConnectClient client)
        {
            StorageFolder folder = ApplicationData.Current.LocalFolder;
            StorageFolder romFolder = await folder.CreateFolderAsync("roms", CreationCollisionOption.OpenIfExists);

            String path = romFolder.Path;

            ROMDatabase db = ROMDatabase.Current;
            var romEntry = db.GetROM(item.Name);
            bool fileExisted = false;
            if (romEntry != null)
            {
                fileExisted = true;
                //MessageBox.Show(String.Format(AppResources.ROMAlreadyExistingError, item.Name), AppResources.ErrorCaption, MessageBoxButton.OK);
                //return;
            }

            var indicator = new ProgressIndicator()
            {
                IsIndeterminate = true,
                IsVisible = true,
                Text = String.Format(AppResources.DownloadingProgressText, item.Name)
            };

            SystemTray.SetProgressIndicator(this, indicator);

            LiveDownloadOperationResult e = await client.DownloadAsync(item.SkyDriveID + "/content");
            if (e != null)
            {
                byte[] tmpBuf = new byte[e.Stream.Length];
                StorageFile destinationFile = await romFolder.CreateFileAsync(item.Name, CreationCollisionOption.ReplaceExisting);
                using (IRandomAccessStream destStream = await destinationFile.OpenAsync(FileAccessMode.ReadWrite))
                using (DataWriter writer = new DataWriter(destStream))
                {
                    while (e.Stream.Read(tmpBuf, 0, tmpBuf.Length) != 0)
                    {
                        writer.WriteBytes(tmpBuf);
                    }
                    await writer.StoreAsync();
                    await writer.FlushAsync();
                    writer.DetachStream();
                }
                e.Stream.Close();
                item.Downloading = false;
                SystemTray.GetProgressIndicator(this).IsVisible = false;
                if (!fileExisted)
                {
                    var entry = FileHandler.InsertNewDBEntry(destinationFile.Name);
                    await FileHandler.FindExistingSavestatesForNewROM(entry);
                    db.CommitChanges();
                }
                MessageBox.Show(String.Format(AppResources.DownloadCompleteText, item.Name));
            }
            else
            {
                SystemTray.GetProgressIndicator(this).IsVisible = false;
                MessageBox.Show(String.Format(AppResources.DownloadErrorText, item.Name, "Api error"), AppResources.ErrorCaption, MessageBoxButton.OK);
            }
        }

        void SkyDriveImportPage_BackKeyPress(object sender, System.ComponentModel.CancelEventArgs e)
        {
            if (this.downloadsInProgress > 0)
            {
                MessageBox.Show(AppResources.ImportWaitComplete);
                e.Cancel = true;
                return;
            }

            if (this.skydriveStack.Count <= 2)
            {
                return;
            }

            try
            {
                // Ordner zurückspringen
                this.skydriveStack.RemoveAt(this.skydriveStack.Count - 1);
                this.skydriveList.ItemsSource = this.skydriveStack.Last();

                string parentName = this.skydriveStack[this.skydriveStack.Count - 2].First(item =>
                {
                    return item.SkyDriveID.Equals(this.skydriveStack[this.skydriveStack.Count - 1][0].ParentID);
                }).Name;

                this.currentFolderBox.Text = parentName;

                e.Cancel = true;
            }
            catch (Exception) { }
        }

        protected async override void OnNavigatedTo(NavigationEventArgs e)
        {
            if (!this.initialPageLoaded)
            {
                try
                {
                    if (this.session != null)
                    {
                        LiveConnectClient client = new LiveConnectClient(this.session);
                        LiveOperationResult result = await client.GetAsync(this.skydriveStack[0][0].SkyDriveID + "/files");
                        this.client_GetCompleted(result);
                        this.statusLabel.Height = 0;
                        this.initialPageLoaded = true;
                    }
                }
                catch (LiveConnectException)
                {
                    this.statusLabel.Height = this.labelHeight;
                    this.statusLabel.Text = AppResources.SkyDriveInternetLost;
                }
            }

            base.OnNavigatedTo(e);
        }

        void client_GetCompleted(LiveOperationResult e)
        {
            if (e != null)
            {
                List<object> list = e.Result["data"] as List<object>;

                if (list == null)
                {
                    return;
                }
                List<SkyDriveListItem> listItems = new List<SkyDriveListItem>();
                foreach (var item in list)
                {
                    
                    IDictionary<string, object> dict = item as IDictionary<string, object>;
                    if (dict == null)
                    {
                        continue;
                    }

                    if (this.skydriveStack.Count == 1)
                    {
                        this.skydriveStack.Last()[0].SkyDriveID = dict["parent_id"].ToString();
                    }

                    String name = dict["name"].ToString();
                    SkyDriveItemType type;
                    if (dict["type"].Equals("folder") || dict["type"].Equals("album"))
                    {
                        type = SkyDriveItemType.Folder;
                    }
                    else
                    {
                        type = SkyDriveItemType.File;
                        int dotIndex = -1;
                        if ((dotIndex = name.LastIndexOf('.')) != -1)
                        {
                            String substrName = name.Substring(dotIndex).ToLower();
#if GBC
                            if (substrName.Equals(".gb") || substrName.Equals(".gbc"))
#else
                            if (substrName.Equals(".gba"))
#endif
                            {
                                type = SkyDriveItemType.ROM;
                            }
                        }
                    }

                    if (type == SkyDriveItemType.File)
                    {
                        continue;
                    }

                    SkyDriveListItem listItem = new SkyDriveListItem()
                    {
                        Name = dict["name"].ToString(),
                        SkyDriveID = dict["id"].ToString(),
                        Type = type,
                        ParentID = dict["parent_id"].ToString()
                    };
                    if (type == SkyDriveItemType.Folder)
                    {
                        int count = 0;
                        int.TryParse(dict["count"].ToString(), out count);
                        listItem.FolderChildrenCount = count;
                    }

                    listItems.Add(listItem);
                }
                this.skydriveStack.Add(listItems);
                this.skydriveList.ItemsSource = listItems;
            }
            else
            {
                MessageBox.Show(String.Format(AppResources.SkyDriveGeneralError, "Api Error"), AppResources.ErrorCaption, MessageBoxButton.OK);
            }
        }

        void ShowBuyDialog()
        {
            var result = MessageBox.Show(AppResources.BuyNowImportText, AppResources.InfoCaption, MessageBoxButton.OKCancel);
            if (result == MessageBoxResult.OK)
            {
                if (this.downloadsInProgress == 0)
                {
                    MarketplaceDetailTask marketplaceDetailTask = new MarketplaceDetailTask();
                    marketplaceDetailTask.ContentType = MarketplaceContentType.Applications;
#if !GBC
                    marketplaceDetailTask.ContentIdentifier = "4e3142c4-b99c-4075-bedc-b10a3086327d";
#else
                    marketplaceDetailTask.ContentIdentifier = "be33ce3e-e519-4d2c-b30e-83347601ed57";                    
#endif
                    marketplaceDetailTask.Show();
                }
                else
                {
                    MessageBox.Show(AppResources.BuyWaitForDownloadText);
                }
            }
        }
    }
}