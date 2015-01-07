#include "Panel.h"
#include "ui/CompositeItem.h"
#include "SlowTicker.h"
#include "screen/Screen.h"
#include "ui/FileShiftRegister.h"
#include "ui/Layout.h"
#include "ui/Link.h"
#include "ui/Group.h"
#include "ui/Cell.h"
#include "ui/Item.h"
#include "ui/Widget.h"
#include "ui/Event.h"
#include "I18n.h"
#include "Icon.h"
#include "Logo.h"
#include "modules/robot/RobotPublicAccess.h"
#include "PlayerPublicAccess.h"
#include "PublicDataRequest.h"
#include "PublicData.h"
#include "TemperatureControlPublicAccess.h"
#include "NetworkPublicAccess.h"
#include "PublicData.h"
#include "checksumm.h"
#include <functional>
#include <tuple>
#include "Kernel.h"
#include "detail/CommandQueue.h"
#include "version.h"
#include "StreamOutputPool.h"
#include "mri.h"

#include "Config.h"
#include "ConfigValue.h"

//FOR SENDING GCODES
#include "Gcode.h"

#include <type_traits>
//#include "ttl/var/variant.hpp"

#include "detail/Zip.h"

#define LOCATION __attribute__ ((section ("AHBSRAM0")))
#define hotend_temp_checksum 	 CHECKSUM("hotend_temperature")
#define bed_temp_checksum    	 CHECKSUM("bed_temperature")
#define hotend_temp_ABS_checksum CHECKSUM("hotend_temperature_ABS")
#define bed_temp_ABS_checksum    CHECKSUM("bed_temperature_ABS")
#define hotend_temp_PLA_checksum CHECKSUM("hotend_temperature_PLA")
#define bed_temp_PLA_checksum    CHECKSUM("bed_temperature_PLA")
#define panel_checksum           CHECKSUM("panel")

namespace info
{
	static Version version; 
	static struct
	{
		void init(Kernel* kernel)
		{
			default_hotend_temperature_PLA = kernel->config->value( panel_checksum, hotend_temp_PLA_checksum )->by_default(185.0f )->as_number();
			default_bed_temperature_PLA    = kernel->config->value( panel_checksum, bed_temp_PLA_checksum    )->by_default(60.0f  )->as_number();
			default_hotend_temperature_ABS = kernel->config->value( panel_checksum, hotend_temp_ABS_checksum )->by_default(235.0f )->as_number();
			default_bed_temperature_ABS    = kernel->config->value( panel_checksum, bed_temp_ABS_checksum    )->by_default(100.0f  )->as_number();	
		}

		float default_bed_temperature_ABS;
		float default_hotend_temperature_ABS;
		float default_bed_temperature_PLA;
		float default_hotend_temperature_PLA;
	} settings = {0};
}

CommandQueue<1> command_buffer;

std::string get_network()
{
    void *returned_data;
    if(PublicData::get_value( network_checksum, get_ip_checksum, &returned_data ))
    {
        uint8_t *ipaddr = (uint8_t *)returned_data;
        char buf[20];
        int n = snprintf(buf, sizeof(buf), "%d.%d.%d.%d", ipaddr[0], ipaddr[1], ipaddr[2], ipaddr[3]);
        buf[n] = 0;
        return buf;
    }
    return "No network";
}

void send_gcode(std::string g)
{
    Gcode gcode(g, &(StreamOutput::NullStream));
    THEKERNEL->call_event(ON_GCODE_RECEIVED, &gcode);
}

template <size_t i>
float get_current_pos_()
{
	void *returned_data;

    bool ok = PublicData::get_value( robot_checksum, current_position_checksum, &returned_data );
    if(ok)
    {
    	float *p = static_cast<float *>(returned_data);
		return p[i];
	}	
	else
	{
		return 0;
	}
}

void set_x_position(float position)
{
	char buffer[9];
	snprintf(buffer, sizeof(buffer), "G0 X%4.0f", position);
	command_buffer.push(std::bind(send_gcode, std::string(buffer)));
}

void set_y_position(float position)
{
	char buffer[9];
	snprintf(buffer, sizeof(buffer), "G0 Y%4.0f", position);
	command_buffer.push(std::bind(send_gcode, std::string(buffer)));
}

void set_z_position(float position)
{
	char buffer[9];
	snprintf(buffer, sizeof(buffer), "G0 Z%4.0f", position);
	command_buffer.push(std::bind(send_gcode, std::string(buffer)));
}

void set_hotend_temperature(float temperature)
{
	PublicData::set_value( temperature_control_checksum, hotend_checksum, &temperature );
}

void set_hotbed_temperature(float temperature)
{
	PublicData::set_value( temperature_control_checksum, bed_checksum, &temperature );
}

std::tuple<float, float> get_hotend_temperature()
{
	void *returned_data;
	if(PublicData::get_value( temperature_control_checksum, hotend_checksum, current_temperature_checksum, &returned_data ))
	{
		pad_temperature t =  *static_cast<struct pad_temperature *>(returned_data);
		return std::make_tuple(t.target_temperature, t.current_temperature);
	}
	else
	{
		return std::make_tuple(0, 0);
	}
}

std::tuple<float, float> get_hotbed_temperature()
{
	void *returned_data;
	if(PublicData::get_value( temperature_control_checksum, bed_checksum, current_temperature_checksum, &returned_data ))
	{
		pad_temperature t =  *static_cast<struct pad_temperature *>(returned_data);
		return std::make_tuple(t.target_temperature, t.current_temperature);
	}
	else
	{
		return std::make_tuple(0, 0);
	}
}

bool is_hotend_hot()
{
	return std::get<1>(get_hotend_temperature()) > 170;
}

bool is_file_being_played()
{
	void* returned_data;
	if(PublicData::get_value(player_checksum, is_playing_checksum, NULL, &returned_data) )
	{
		return *static_cast<bool*>(returned_data);
	}
	else
	{
		return false;
	}
}

void abort_playing_file()
{
	PublicData::set_value(player_checksum, abort_play_checksum, 0);
}

std::tuple<uint32_t, std::string> get_progress()
{
    void *returned_data;
    if (PublicData::get_value( player_checksum, get_progress_checksum, &returned_data ))
    {
    	pad_progress p =  *static_cast<struct pad_progress *>(returned_data);
    	size_t name_start = p.filename.find_last_of("/") + 1;
    	size_t name_end   = p.filename.find_last_of(".");
        return(std::make_tuple(p.percent_complete, p.filename.substr(name_start, name_end-name_start) ));
    } else {
		return(std::make_tuple(0, i18n::no_file_caption));
    }
}

uint32_t estimate_remaining(uint32_t seconds_elapsed, uint32_t percent_complete)
{
	if(percent_complete > 3)
	{
		return (seconds_elapsed * 100)/(percent_complete) - seconds_elapsed;
	}
	else
	{
		return 0;
	}
}

std::tuple<uint32_t, uint32_t> get_time_progress()
{
    void *returned_data;
    if (PublicData::get_value( player_checksum, get_progress_checksum, &returned_data )) 
    {
    	pad_progress p =  *static_cast<struct pad_progress *>(returned_data);
        return(std::make_tuple(p.elapsed_secs, estimate_remaining(p.elapsed_secs, p.percent_complete) ));
    } else {
		return(std::make_tuple(0, 0));
    }
}

ui::GlobalCell const scroll_bar_cells[] = 
{
	ui::GlobalCell({/*x*/  120,  /*y*/ 0, /*w*/ 8, /*h*/ 64}, ui::DefaultGlobalRenderPolicy())
};

ui::Cell const main_menu_cells[] = 
{
	ui::Cell({/*x*/  0,  /*y*/   0, /*w*/ 43, /*h*/ 32}, ui::DefaultRenderPolicy(), ui::ActiveRenderPolicy()),
	ui::Cell({/*x*/  0,  /*y*/  32, /*w*/ 43, /*h*/ 32}, ui::DefaultRenderPolicy(), ui::ActiveRenderPolicy()),
	ui::Cell({/*x*/ 43,  /*y*/   0, /*w*/ 43, /*h*/ 32}, ui::DefaultRenderPolicy(), ui::ActiveRenderPolicy()),
	ui::Cell({/*x*/ 43,  /*y*/  32, /*w*/ 43, /*h*/ 32}, ui::DefaultRenderPolicy(), ui::ActiveRenderPolicy()),
	ui::Cell({/*x*/ 86,  /*y*/   0, /*w*/ 42, /*h*/ 32}, ui::DefaultRenderPolicy(), ui::ActiveRenderPolicy()),
	ui::Cell({/*x*/ 86,  /*y*/  32, /*w*/ 42, /*h*/ 32}, ui::DefaultRenderPolicy(), ui::ActiveRenderPolicy())
};

ui::Cell const status_menu_cells[] = 
{
	ui::Cell({/*x*/ 0,  /*y*/   0, /*w*/ 128, /*h*/ 16}, ui::DefaultRenderPolicy(), ui::ActiveRenderPolicy()),
	ui::Cell({/*x*/ 0,  /*y*/  16, /*w*/ 128, /*h*/ 16}, ui::DefaultRenderPolicy(), ui::ActiveRenderPolicy()),
	ui::Cell({/*x*/ 0,  /*y*/  32, /*w*/ 128, /*h*/ 16}, ui::DefaultRenderPolicy(), ui::ActiveRenderPolicy()),
	ui::Cell({/*x*/ 0,  /*y*/  48, /*w*/ 128, /*h*/ 16}, ui::DefaultRenderPolicy(), ui::ActiveRenderPolicy())
};

ui::Cell const stacked_menu_cells[] = 
{
	ui::Cell({/*x*/ 2,  /*y*/   0, /*w*/ 124, /*h*/ 21}, ui::DefaultRenderPolicy(), ui::ActiveRenderPolicy()),
	ui::Cell({/*x*/ 2,  /*y*/  21, /*w*/ 124, /*h*/ 22}, ui::DefaultRenderPolicy(), ui::ActiveRenderPolicy()),
	ui::Cell({/*x*/ 2,  /*y*/  42, /*w*/ 124, /*h*/ 22}, ui::DefaultRenderPolicy(), ui::ActiveRenderPolicy())
};

ui::Cell const modal_menu_cells[] = 
{
	ui::Cell({/*x*/ 0,  /*y*/   0, /*w*/ 128, /*h*/ 32}, ui::DefaultRenderPolicy(), ui::ActiveRenderPolicy()),
	ui::Cell({/*x*/ 0,  /*y*/  32, /*w*/ 128, /*h*/ 32}, ui::DefaultRenderPolicy(), ui::ActiveRenderPolicy())
};

ui::Cell const splash_cell[] = 
{
	ui::Cell({/*x*/ 0,  /*y*/   0, /*w*/ 128, /*h*/ 64}, ui::DefaultRenderPolicy(), ui::ActiveRenderPolicy())
};

ui::Layout default_layout(main_menu_cells);
ui::Layout stacked_layout(stacked_menu_cells);
ui::Layout status_layout(status_menu_cells);
ui::Layout splash_layout(splash_cell);
ui::Layout modal_layout(modal_menu_cells);

enum class MainMenu : size_t
{
	Move,
	Heat,
	Print,
	Maintenance,
	Status,
	Options
};

enum class MoveMenu : size_t
{
	Back,
	Home,
	Z,
	X,
	Y
};

enum class ManualHeatMenu : size_t
{
	Back,
	Hotend,
	Hotbed
};

enum class SetupMenu : size_t
{
	HotendTemperature,
	HotbedTemperature,
	ZPosition,
	PlayerStatus
};

enum class HeatMenu : size_t
{
	Back,
	PreheatAbs,
	PreheatPla,
	ManualPreheat,
	CoolDown
};

enum class HomeMenu : size_t
{
	Back,
	Z,
	XY,
	XYZ
};

enum class StatusMenu : size_t
{
	HotendTemperature,
	HotbedTemperature,
	Z,
	Progress	
};

enum class MaintenanceMenu : size_t
{
	Back,
	ManualExtrusion,
	PrimePrinthead,
	Motors
	//FilamentChange,
	//LevelBed
};

enum class ExtrudeMenu : size_t
{
	Back,
	Extrude,
	Retract
};

enum class OptionsMenu : size_t
{
	Back,
	Ip,
	Version
};

CompositeItem LOCATION logo_menu_items[] = 
{
	ui::LogoItem(i18n::back_caption, picture::logo, 18)
};

CompositeItem LOCATION status_menu_items[] = 
{
	ui::ProgressInfo(i18n::progress_caption, get_progress),
	ui::TimeInfo(i18n::progress_caption, get_time_progress),
	ui::FloatFloatInfo(i18n::hotend_temperature_caption, get_hotend_temperature),
	ui::FloatFloatInfo(i18n::hotbed_temperature_caption, get_hotbed_temperature),
};

void preheat_abs()
{
	set_hotend_temperature(info::settings.default_hotend_temperature_ABS); 
	set_hotbed_temperature(info::settings.default_bed_temperature_ABS);
}


void preheat_pla()
{
	set_hotend_temperature(info::settings.default_hotend_temperature_PLA); 
	set_hotbed_temperature(info::settings.default_bed_temperature_PLA);
}


void cool_down()
{
	set_hotend_temperature(0); 
	set_hotbed_temperature(0);
}

CompositeItem LOCATION heat_menu_items[] = 
{
	ui::Item(i18n::back_caption),
	ui::Command(i18n::preheat_abs_caption, preheat_abs),
	ui::Command(i18n::preheat_pla_caption, preheat_pla),
	ui::Item(i18n::manual_preheat_caption),
	ui::Command(i18n::cool_down_caption, cool_down)
};

CompositeItem LOCATION manual_heat_menu_items[] = 
{
	ui::Item(i18n::back_caption),
	ui::HeatControl(i18n::hotend_temperature_caption, get_hotend_temperature, set_hotend_temperature, 150),
	ui::HeatControl(i18n::hotbed_temperature_caption, get_hotbed_temperature, set_hotbed_temperature, 0)
};

void home_z()
{
	send_gcode("G28 Z");
}

void buffered_home_z()
{
	command_buffer.push(home_z);
}

void home_xy()
{
	send_gcode("G28 XY");
}

void buffered_home_xy()
{
	command_buffer.push(home_xy);
}

void home_xyz()
{
	send_gcode("G28 XYZ");
}

void buffered_home_xyz()
{
	command_buffer.push(home_xyz);
}

/*
	[CompI, CompI, CompI]
*/

CompositeItem LOCATION home_menu_items[] = 
{
	ui::Item(i18n::back_caption),
	ui::Command(i18n::home_z_caption, buffered_home_z),
	ui::Command(i18n::home_xy_caption, buffered_home_xy),
	ui::Command(i18n::home_xyz_caption, buffered_home_xyz)
};

CompositeItem LOCATION move_menu_items[] = 
{
	ui::Item(i18n::back_caption),
	ui::Item(i18n::home_caption),
	ui::PositionControl(i18n::z_caption, get_current_pos_<2>, set_z_position),
	ui::PositionControl(i18n::x_caption, get_current_pos_<0>, set_x_position),
	ui::PositionControl(i18n::y_caption, get_current_pos_<1>, set_y_position)
};

CompositeItem LOCATION main_menu_items[] = 
{
	ui::GraphicalItem(i18n::move_caption, icon::move),
	ui::GraphicalItem(i18n::heat_caption, icon::heat),
	ui::GraphicalItem(i18n::print_caption, icon::print),
	ui::GraphicalItem(i18n::maintenance_caption, icon::service),
	ui::GraphicalItem(i18n::status_caption, icon::status),
	ui::GraphicalItem(i18n::settings_caption, icon::setup)
};

void buffered_avoid_playing_file()
{
	command_buffer.push(abort_playing_file);
}

CompositeItem LOCATION abort_menu_items[] = 
{
	ui::Command(i18n::abort_print_caption, buffered_avoid_playing_file),
	ui::Item(i18n::not_abort_print_caption)
};

ui::FileShiftRegister file_browser;

CompositeItem LOCATION file_menu_items[] = 
{
	ui::File(file_browser, 0),
	ui::File(file_browser, 1),
	ui::File(file_browser, 2)
};

void prime_printhead()
{
	send_gcode("G91");
	send_gcode("G1 E60 F100"); //change to 100 for material change
	send_gcode("G90");
}

void buffered_prime_printhead()
{
	command_buffer.push(prime_printhead);
}

CompositeItem LOCATION maintenance_menu_items[] = 
{
	ui::Item(i18n::back_caption),
	ui::Item(i18n::manual_extrusion_caption),
	ui::ConditionalCommand(i18n::prime_printhead_caption, is_hotend_hot, buffered_prime_printhead),
	ui::Item(i18n::motors_caption),
//	ui::Item(i18n::leds_caption)
//	ui::Item(i18n::filament_change_caption),
//	ui::Item(i18n::level_bed_caption)
};


void extrude()
{
	send_gcode("G91");
	send_gcode("G1 E5 F100");
	send_gcode("G90");
}

void retract()
{
	send_gcode("G91");
	send_gcode("G1 E-5 F100");
	send_gcode("G90");
}

void buffered_extrude()
{
	command_buffer.push(extrude);
}

void buffered_retract()
{
	command_buffer.push(retract);
}

CompositeItem LOCATION extrude_menu_items[] = 
{
	ui::Item(i18n::back_caption),
	ui::Command(i18n::extrude_caption, buffered_extrude),
	ui::Command(i18n::retract_caption, buffered_retract)
};

CompositeItem LOCATION options_menu_items[] = 
{
	ui::Item(i18n::back_caption),
	ui::CharInfo(i18n::ip_caption, get_network),
	//ui::CharInfo(i18n::version_caption, []{return info::version.get_build();})
};

CompositeItem LOCATION init_menu_items[] = 
{
	ui::Command(i18n::init_home, buffered_home_xyz),
	ui::Item(i18n::dont_home),
};

void turn_motors_off()
{
	send_gcode("M18");
}

void turn_motors_on()
{
	send_gcode("M17");
}

void buffered_turn_motors_on()
{
	command_buffer.push(turn_motors_on);
}

void buffered_turn_motors_off()
{
	command_buffer.push(turn_motors_off);
}

CompositeItem LOCATION motors_menu_items[] = 
{
	ui::Item(i18n::back_caption),
	ui::Command(i18n::turn_motors_on_caption, buffered_turn_motors_on),
	ui::Command(i18n::turn_motors_off_caption, buffered_turn_motors_off)
};

CompositeItem LOCATION cold_extrusion_splash_items[] = 
{
	ui::Item(i18n::cold_extrusion_prevented_caption)
};

ui::Widget logo_menu_widget(&splash_layout);
ui::Widget init_menu_widget(&modal_layout);
ui::Widget main_menu_widget(&default_layout);
ui::Widget move_menu_widget(&stacked_layout); // correctness is not checked during compilation
ui::Widget file_menu_widget(&stacked_layout);
ui::Widget abort_menu_widget(&modal_layout);
ui::Widget heat_menu_widget(&stacked_layout);
ui::Widget home_menu_widget(&stacked_layout);
ui::Widget manual_heat_menu_widget(&stacked_layout);
ui::Widget status_menu_widget(&status_layout);
ui::Widget maintenance_menu_widget(&stacked_layout);
ui::Widget extrude_menu_widget(&stacked_layout);
ui::Widget options_menu_widget(&stacked_layout);
ui::Widget motors_menu_widget(&stacked_layout);
ui::Widget cold_extrusion_splash_widget(&splash_layout);

ui::Link LOCATION logo_menu_links[1];
ui::Link LOCATION init_menu_links[2];
ui::Link LOCATION main_menu_links[6];
ui::Link LOCATION move_menu_links[5];
ui::Link LOCATION file_menu_links[3];
ui::Link LOCATION heat_menu_links[5];
ui::Link LOCATION home_menu_links[4];
ui::Link LOCATION abort_menu_links[2];
ui::Link LOCATION manual_heat_menu_links[3];
ui::Link LOCATION status_menu_links[4];
ui::Link LOCATION maintenance_menu_links[4];
ui::Link LOCATION extrude_menu_links[3];
ui::Link LOCATION options_menu_links[2];
ui::Link LOCATION motors_menu_links[3];
ui::Link LOCATION cold_extrusion_splash_links[1];

ui::Group LOCATION logo(logo_menu_items, logo_menu_links, logo_menu_widget);
ui::Group LOCATION init_menu(init_menu_items, init_menu_links, init_menu_widget);
ui::Group LOCATION main_menu(main_menu_items, main_menu_links, main_menu_widget);
ui::Group LOCATION move_menu(move_menu_items, move_menu_links, move_menu_widget);
ui::Group LOCATION file_menu(file_menu_items, file_menu_links, file_menu_widget);
ui::Group LOCATION abort_menu(abort_menu_items, abort_menu_links, abort_menu_widget);
ui::Group LOCATION heat_menu(heat_menu_items, heat_menu_links, heat_menu_widget);
ui::Group LOCATION home_menu(home_menu_items, home_menu_links, home_menu_widget);
ui::Group LOCATION manual_heat_menu(manual_heat_menu_items, manual_heat_menu_links, manual_heat_menu_widget);
ui::Group LOCATION status_menu(status_menu_items, status_menu_links, status_menu_widget);
ui::Group LOCATION maintenance_menu(maintenance_menu_items, maintenance_menu_links, maintenance_menu_widget);
ui::Group LOCATION extrude_menu(extrude_menu_items, extrude_menu_links, extrude_menu_widget);
ui::Group LOCATION options_menu(options_menu_items, options_menu_links, options_menu_widget);
ui::Group LOCATION motors_menu(motors_menu_items, motors_menu_links, motors_menu_widget);
ui::Group LOCATION cold_extrusion_splash(cold_extrusion_splash_items, cold_extrusion_splash_links, cold_extrusion_splash_widget);

Panel::Panel()
:up_button(10, 5), down_button(10, 5), select_button(10, 5), user_interface(logo.get_link_to(0), screen)
{
	// THEKERNEL->streams->printf("\r\n");
	// THEKERNEL->streams->printf("%s\r\n", std::is_literal_type<ui::Item>::value ? "true" : "false");
	// THEKERNEL->streams->printf("%s\r\n", std::is_literal_type<ui::Control<uint8_t>>::value ? "true" : "false");
	// THEKERNEL->streams->printf("%s\r\n", std::is_literal_type<ttl::var::variant<uint32_t, char[20]>>::value ? "true" : "false");
	// THEKERNEL->streams->printf("Sizeof HeatItem: %u.\r\n", sizeof(ui::HeatControl));
	// THEKERNEL->streams->printf("Sizeof Link: %u.\r\n", sizeof(ui::Link));
	// THEKERNEL->streams->printf("Sizeof Dupa Link: %u.\r\n", sizeof(ui::DupaLink));
	// THEKERNEL->streams->printf("Sizeof Cell: %u.\r\n", sizeof(ui::Cell));
	// THEKERNEL->streams->printf("Sizeof Composite: %u.\r\n", sizeof(CompositeItem));
	info::settings.init(THEKERNEL);

	init_menu.set_link_for(index(0), status_menu.get_link_to(0));
	init_menu.set_link_for(index(1), status_menu.get_link_to(0));
	file_browser.open_directory("/");
	main_menu.set_link_for(index(MainMenu::Move), move_menu.get_link_to(index(MoveMenu::Back)));
	main_menu.set_link_for(index(MainMenu::Heat), heat_menu.get_link_to(index(HeatMenu::Back)));
	main_menu.set_link_for(index(MainMenu::Print), ui::Link(is_file_being_played, 0, &abort_menu, 0, &file_menu) );
	main_menu.set_link_for(index(MainMenu::Status), status_menu.get_link_to(0));
	main_menu.set_link_for(index(MainMenu::Maintenance), maintenance_menu.get_link_to(index(MaintenanceMenu::Back)) );
	main_menu.set_link_for(index(MainMenu::Options), options_menu.get_link_to(index(OptionsMenu::Back)) );

	move_menu.set_link_for(index(MoveMenu::Back), main_menu.get_link_to(index(MainMenu::Move)));
	move_menu.set_link_for(index(MoveMenu::Home), home_menu.get_link_to(index(HomeMenu::Back)));
	home_menu.set_link_for(index(HomeMenu::Back), move_menu.get_link_to(index(MoveMenu::Back)));
	
	file_menu.set_link_for(0, status_menu.get_link_to(0));
	file_menu.set_link_for(1, status_menu.get_link_to(0));
	file_menu.set_link_for(2, status_menu.get_link_to(0));

	heat_menu.set_link_for(index(HeatMenu::Back), main_menu.get_link_to(index(MainMenu::Heat)));
	heat_menu.set_link_for(index(HeatMenu::ManualPreheat), manual_heat_menu.get_link_to(index(ManualHeatMenu::Hotend)));
	
	manual_heat_menu.set_link_for(index(ManualHeatMenu::Back), heat_menu.get_link_to(index(HeatMenu::Back) ));
	maintenance_menu.set_link_for(index(MaintenanceMenu::Back), main_menu.get_link_to(index(MainMenu::Maintenance)) );
	maintenance_menu.set_link_for(index(MaintenanceMenu::ManualExtrusion), ui::Link( is_hotend_hot, index(ExtrudeMenu::Back), &extrude_menu, 0, &cold_extrusion_splash) );
	maintenance_menu.set_link_for(index(MaintenanceMenu::PrimePrinthead), cold_extrusion_splash.get_link_to(0) );
	maintenance_menu.set_link_for(index(MaintenanceMenu::Motors), motors_menu.get_link_to(0) );
	extrude_menu.set_link_for(index(ExtrudeMenu::Back), maintenance_menu.get_link_to(index(MaintenanceMenu::Back)));
	
	motors_menu.set_link_for(0, maintenance_menu.get_link_to(index(MaintenanceMenu::Back) ));
	status_menu.set_link_for(0, main_menu.get_link_to(index(MainMenu::Status)));
	status_menu.set_link_for(1, main_menu.get_link_to(index(MainMenu::Status)));
	status_menu.set_link_for(2, main_menu.get_link_to(index(MainMenu::Status)));
	status_menu.set_link_for(3, main_menu.get_link_to(index(MainMenu::Status)));

	abort_menu.set_link_for(0, status_menu.get_link_to(0) );
	abort_menu.set_link_for(1, main_menu.get_link_to(index(MainMenu::Print)) );

	options_menu.set_link_for(index(OptionsMenu::Back), main_menu.get_link_to(index(MainMenu::Options)));

	cold_extrusion_splash.set_link_for(0, heat_menu.get_link_to(index(HeatMenu::Back)));
	logo.set_link_for(0, main_menu.get_link_to(0));


	//min_extrusion_temperature = THEKERNEL->config->value(extruder_checksum, this->identifier, min_temp_checksum )->by_default(170)->as_number(); // REMOVE REDUNTANT DATA IN THE FUTURE
    //max_temperature = THEKERNEL->config->value(extruder_checksum, this->identifier, max_temp_checksum )->by_default(300)->as_number(); // REMOVE REDUNTANT DATA IN THE FUTURE
 //    default_hotend_temperature 	   = THEKERNEL->config->value( panel_checksum, hotend_temp_checksum )->by_default(185.0f )->as_number();
 //    default_bed_temperature    	   = THEKERNEL->config->value( panel_checksum, bed_temp_checksum    )->by_default(60.0f  )->as_number();
	// default_hotend_temperature_ABS = THEKERNEL->config->value( panel_checksum, hotend_temp_ABS_checksum )->by_default(235.0f )->as_number();
 //    default_bed_temperature_ABS    = THEKERNEL->config->value( panel_checksum, bed_temp_ABS_checksum    )->by_default(100.0f  )->as_number();
	// default_hotend_temperature_PLA = THEKERNEL->config->value( panel_checksum, hotend_temp_PLA_checksum )->by_default(185.0f )->as_number();
 //    default_bed_temperature_PLA    = THEKERNEL->config->value( panel_checksum, bed_temp_PLA_checksum    )->by_default(60.0f  )->as_number();

}

void Panel::on_module_loaded()
{
	this->register_for_event(ON_IDLE);
    this->register_for_event(ON_MAIN_LOOP);
    this->register_for_event(ON_SECOND_TICK);

    THEKERNEL->slow_ticker->attach( 23U,  this,  &Panel::button_tick );
    THEKERNEL->slow_ticker->attach( 50,   this,  &Panel::refresh_tick );
}

uint32_t Panel::button_tick(uint32_t dummy)
{
	this->button_state = this->screen.read_buttons();
	if(this->button_state & BUTTON_UP)
	{
		up_button.press();
	}
	else
	{
		up_button.tick();
	}
	if(this->button_state & BUTTON_DOWN)
	{
		down_button.press();
	}
	else
	{
		down_button.tick();
	}
	if(this->button_state & BUTTON_SELECT)
	{
		select_button.press();
	}
	else
	{
		select_button.tick();
	}
	return 0;
}

uint32_t Panel::refresh_tick(uint32_t dummy)
{
	this->refresh_flag = true;
	return 0;
}

void Panel::on_main_loop(void* argument)
{
	while(execute_next(command_buffer)){};
	if(refresh_flag)
	{
		refresh_flag = false;
		user_interface.render();
		user_interface.refresh();
	}
}

void Panel::on_second_tick(void* argument)
{
	this->tick_flag = true;
}

void Panel::on_idle(void* argument)
{
	if(tick_flag)
	{
		tick_flag = false;
		user_interface.dispatch(ui::TickEvent());
	}
	if(up_button.read())
	{
		user_interface.dispatch(ui::UpEvent());
	}
	if(down_button.read())
	{
		user_interface.dispatch(ui::DownEvent());
	}
	if(select_button.read())
	{
		screen.clear();
		user_interface.dispatch(ui::OkEvent());
	}
	if(refresh_flag)
	{
		refresh_flag = false;
		user_interface.render();
		user_interface.refresh();
	}
}