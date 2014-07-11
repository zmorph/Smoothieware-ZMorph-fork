/*
      This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl).
      Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
      Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
      You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/

#include "libs/Kernel.h"
#include "Panel.h"
#include "PanelScreen.h"
#include "MainMenuScreen.h"
#include "FileScreen.h"
#include "libs/nuts_bolts.h"
#include "libs/utils.h"
#include <string>
#include "libs/SerialMessage.h"
#include "StreamOutput.h"
#include "DirHandle.h"
#include "mri.h"

using namespace std;

FileScreen::FileScreen()
{
    this->current_folder = "";
    this->start_play = false;
}

// When entering this screen
void FileScreen::on_enter()
{
    THEPANEL->lcd->clear();

    // Default folder to enter
    if ( this->current_folder.compare("") == 0 ) {
        this->enter_folder("/");
    } else {
        this->enter_folder(this->current_folder);
    }
}

// For every ( potential ) refresh of the screen
void FileScreen::on_refresh()
{
    if ( THEPANEL->menu_change() ) {
        this->refresh_menu();
    }
    if ( THEPANEL->click() ) {
        this->clicked_line(THEPANEL->get_menu_current_line());
    }
}

// Enter a new folder
void FileScreen::enter_folder(std::string folder)
{

    // Rembember where we are
    this->current_folder = folder;

    // We need the number of lines to setup the menu
	current_folder_content.clear();
    this->browse_folder_content(this->current_folder);
	uint16_t number_of_files_in_folder = current_folder_content.size();

    // Setup menu
    THEPANEL->setup_menu(number_of_files_in_folder + 1); // same number of files as menu items
    THEPANEL->enter_menu_mode();

    // Display menu
    this->refresh_menu();
}

// Called by the panel when refreshing the menu, display .. then all files in the current dir
void FileScreen::display_menu_line(uint16_t line)
{
    if ( line == 0 ) {
        THEPANEL->lcd->printf("..");
    } else {
			THEPANEL->lcd->printf("%s", (this->file_at(line - 1).substr(0, 18)).c_str());
    }
}

// When a line is clicked in the menu, act
void FileScreen::clicked_line(uint16_t line)
{
    if ( line == 0 ) {
        if ( this->current_folder.compare("/") == 0 ) {
            // Exit file navigation
			current_folder_content.clear();
            THEPANEL->enter_screen(this->parent);
        } else {
            // Go up one folder
            this->current_folder = this->current_folder.substr(0, this->current_folder.find_last_of('/') + 1);
            if ( this->current_folder[this->current_folder.length() - 1] == '/' && this->current_folder.length() != 1 ) {
                this->current_folder.erase(this->current_folder.length() - 1, 1);
            }
            this->enter_folder(this->current_folder);
        }
    } else {
        //printf("enter file\r\n");
        // Enter file
        string path = this->current_folder;
        if ( path.compare("/") == 0 ) {
            path = "";
        }
        path = path + "/" + this->file_at( line - 1 );
        if ( this->is_a_folder( path ) ) {
            this->enter_folder(path);
            return;
        }

        // start printing that file...
		if(this->is_a_gcode(path))
		{
			this->play_path = path;
			this->start_play = true;
		}
    }

}

//check if a file has .g or .gcode extension
bool FileScreen::is_a_gcode(string path)
{
	string extension = lc(path.substr(path.find_last_of(".") + 1));
	if(extension == "g" || extension == "gcode")
		return true;
	else return false;
}

bool FileScreen::is_no_ext_file(string path)
{
	if (path.find_last_of(".") == string::npos) return true;
	else return false;
}

// Check wether a line is a folder or a file
bool FileScreen::is_a_folder( string path )
{
    // In the special case of /local/ ( the mbed flash chip ) we don't have sub-folders, everything is a file
    if ( path.substr(0, 7).compare("/local/") == 0 ) {
        return false;
    }
    // Else, check if it's a folder or not
    DIR *d;
    d = opendir(path.c_str());
    if (d == NULL) {
        return false;
    } else {
        closedir(d);
        return true;
    }
}

// Find the "line"th file in the current folder
string FileScreen::file_at(uint16_t line)
{
	if(current_folder_content.size() > line) return current_folder_content[line];
	else return "";
}

//compares string to banned file names
bool FileScreen::is_acceptable(std::string name)
{
	if(name == "config" || name == "System Volume Information") return false;
	if(is_a_gcode(name) || is_no_ext_file(name))
		return true;
	else return false;
}

// Browse the current location and push files and folder to vector
void FileScreen::browse_folder_content(std::string folder)
{
    DIR *d;
    struct dirent *p;
    d = opendir(folder.c_str());
    if (d != NULL) {
        while ((p = readdir(d)) != NULL) {
			if(is_acceptable(p->d_name))
				current_folder_content.push_back(p->d_name);
        }
        closedir(d);
    }
}
void FileScreen::on_main_loop()
{
    if (this->start_play) {
        this->start_play = false;
        THEPANEL->set_playing_file(this->play_path);
        this->play(this->play_path);
        THEPANEL->enter_screen(this->parent);
        return;
    }
}

void FileScreen::play(string path)
{
    struct SerialMessage message;
    message.message = string("play ") + path + " -q";
    message.stream = &(StreamOutput::NullStream);
    THEKERNEL->call_event(ON_CONSOLE_LINE_RECEIVED, &message );
}
