﻿//
// CheatPane.xaml.h
// Declaration of the CheatPane class
//

#pragma once

#include "CheatPane.g.h"
#include "CheatData.h"

namespace Win8Snes9x
{
	[Windows::Foundation::Metadata::WebHostHidden]
	public ref class CheatPane sealed
	{
	public:
		property bool Cancelled;

		CheatPane();
	private:
		Vector<CheatData ^> ^cheatCodes;

		void DeleteCheatButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void backbutton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);

		void Close(void);

		void RefreshCheatList(void);
		bool CheckCodeFormat(Platform::String ^codeText, void (*messageCallback)(Platform::String ^));
		Vector<Platform::String ^> ^GetCodes(Platform::String ^code);

		void addButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void enableCheatBox_Checked(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
	};
}
