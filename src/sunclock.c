#include "pebble_os.h"
#include "pebble_app.h"
#include "pebble_fonts.h"
#include "config.h"
#include "my_math.h"
#include "suncalc.h"
#include "http.h"

//#define MY_UUID {0x91,0x41,0xB6,0x28,0xBC,0x89,0x49,0x8E,0xB1,0x47,0x44,0x17,0xE0,0x5C,0xDE,0xD9}
#define MY_UUID {0xA6,0x85,0xE5,0x63,0xEE,0xBA,0x46,0x26,0x91,0x9B,0x5E,0x8F,0x73,0x93,0x0C,0x13}

#if ANDROID
PBL_APP_INFO(MY_UUID,
             "Twilight-Clock", "MichaelEhrmann,KarbonPebbler,Boldo,Chad Harp",
             2, 0, /* App version */
             RESOURCE_ID_IMAGE_MENU_ICON,
             APP_INFO_WATCH_FACE);
# else
PBL_APP_INFO(HTTP_UUID,
             "Twilight-Clock", "MichaelEhrmann,KarbonPebbler,Boldo,Chad Harp",
             2, 0, /* App version */
             RESOURCE_ID_IMAGE_MENU_ICON,
             APP_INFO_WATCH_FACE);
#endif


Window window;

#if HOUR_VIBRATION
const VibePattern hour_pattern = {
        .durations = (uint32_t []) {200, 100, 200, 100, 200},
        .num_segments = 5
};
#endif
	
TextLayer text_time_layer;
TextLayer text_sunrise_layer;
TextLayer text_sunset_layer;
TextLayer dow_layer;
TextLayer mon_layer;
TextLayer moonLayer; // moon phase
Layer graphics_night_layer;
//Make fonts global so we can deinit later
GFont font_roboto;
GFont font_moon;
float realTimezone = TIMEZONE;
float realLatitude = LATITUDE;
float realLongitude = LONGITUDE;

RotBmpPairContainer bitmap_container;
RotBmpPairContainer watchface_container;
BmpContainer light_grey;
BmpContainer grey;
BmpContainer dark_grey;

GPathInfo night_path_info = {
  5,
  (GPoint []) {
    {0, 9},
    {-73, +84}, //replaced by astronomical dawn angle
    {-73, +84}, //bottom left
    {+73, +84}, //bottom right
    {+73, +84}, //replaced by astronomical dusk angle
  }
};

GPathInfo aTwilight_path_info = {
  5,
  (GPoint []) {
    {0, 9},
    {-73, -84}, //replaced by nautical dawn angle
    {-73, -84}, //top left
    {+73, -84}, //top right
    {+73, -84}, //replaced by nautical dusk angle
  }
};

GPathInfo nTwilight_path_info = {
  5,
  (GPoint []) {
    {0, 9},
    {-73, -84}, //replaced by civil dawn angle
    {-73, -84}, //top left
    {+73, -84}, //top right
    {+73, -84}, //replaced by civil dusk angle
  }
};

GPathInfo cTwilight_path_info = {
  5,
  (GPoint []) {
    {0, 9},
    {-73, -84}, //replaced by sunrise angle
    {-73, -84}, //top left
    {+73, -84}, //top right
    {+73, -84}, //replaced by sunset angle
  }
};

GPath night_path;     //Night = black
GPath aTwilight_path; //Astronomical Twilight = dark_grey
GPath nTwilight_path; //Nautical Twilight = grey
GPath cTwilight_path; //Civil Twilight = light_grey

short currentData = -1;

void graphics_night_layer_update_callback(Layer *me, GContext* ctx) 
{
  (void)me;

  gpath_init(&night_path, &night_path_info);
  gpath_move_to(&night_path, grect_center_point(&graphics_night_layer.frame));

  graphics_context_set_fill_color(ctx, GColorBlack);
  gpath_draw_filled(ctx, &night_path);  
  
  graphics_context_set_compositing_mode(ctx, GCompOpAnd);

  bmp_init_container(RESOURCE_ID_IMAGE_DARK_GREY, &dark_grey);
  graphics_draw_bitmap_in_rect(ctx, &dark_grey.bmp, GRect(0,0,144,168));
  gpath_init(&aTwilight_path, &aTwilight_path_info);
  gpath_move_to(&aTwilight_path, grect_center_point(&graphics_night_layer.frame));
  graphics_context_set_fill_color(ctx, GColorWhite);
  gpath_draw_filled(ctx, &aTwilight_path);  
	
  bmp_init_container(RESOURCE_ID_IMAGE_GREY, &grey);
  graphics_draw_bitmap_in_rect(ctx, &grey.bmp, GRect(0,0,144,168));
  gpath_init(&nTwilight_path, &nTwilight_path_info);
  gpath_move_to(&nTwilight_path, grect_center_point(&graphics_night_layer.frame));
  graphics_context_set_fill_color(ctx, GColorWhite);
  gpath_draw_filled(ctx, &nTwilight_path);  

  bmp_init_container(RESOURCE_ID_IMAGE_LIGHT_GREY, &light_grey);
  graphics_draw_bitmap_in_rect(ctx, &light_grey.bmp, GRect(0,0,144,168));
  gpath_init(&cTwilight_path, &cTwilight_path_info);
  gpath_move_to(&cTwilight_path, grect_center_point(&graphics_night_layer.frame));
  graphics_context_set_fill_color(ctx, GColorWhite);
  gpath_draw_filled(ctx, &cTwilight_path);  
	
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
}

float get24HourAngle(int hours, int minutes) 
{
  return (12.0f + hours + (minutes/60.0f)) / 24.0f;
}

void adjustTimezone(float* time) 
{
  *time += realTimezone;
  if (*time > 24) *time -= 24;
  if (*time < 0) *time += 24;
}

//return julian day number for time
int tm2jd(PblTm *time)
{
    int y,m,d,a,b,c,e,f;
    PblTm now;
    get_time (&now);
    y = now.tm_year + 1900;
    m = now.tm_mon + 1;
    d = now.tm_mday;
    if (m < 3) 
    {
	m += 12;
	y -= 1;
    }
    a = y/100;
    b = a/4;
    c = 2 - a + b;
    e = 365.25*(y + 4716);
    f = 30.6001*(m + 1);
    return c+d+e+f-1524;
}


int moon_phase(int jdn)
{
    double jd;
    jd = jdn-2451550.1;
    jd /= 29.530588853;
    jd -= (int)jd;
    return (int)(jd*27 + 0.5); // scale fraction from 0-27 and round by adding 0.5 
}  


// Called once per day
void handle_day(AppContextRef ctx, PebbleTickEvent *t) {

    (void)t;
    (void)ctx;


    static char moon[] = "m";
    int moonphase_number = 0;

    PblTm *time = t->tick_time;
    if(!t)
        get_time(time);

    // moon
    moonphase_number = moon_phase(tm2jd(time));
    // correct for southern hemisphere
    if ((moonphase_number > 0) && (realLatitude < 0))
        moonphase_number = 28 - moonphase_number;
    // select correct font char
    if (moonphase_number == 14)
    {
        moon[0] = (unsigned char)(48);
    } else if (moonphase_number == 0) {
        moon[0] = (unsigned char)(49);
    } else if (moonphase_number < 14) {
        moon[0] = (unsigned char)(moonphase_number+96);
    } else {
        moon[0] = (unsigned char)(moonphase_number+95);
    }
//    moon[0] = (unsigned char)(moonphase_number);
    text_layer_set_text(&moonLayer, moon);
}



void updateDayAndNightInfo(bool update_everything)
{
  static char sunrise_text[] = "00:00";
  static char sunset_text[] = "00:00";

  PblTm pblTime;
  get_time(&pblTime);

  if(update_everything || currentData != pblTime.tm_hour) 
  {
    char *time_format;

    if (clock_is_24h_style()) 
    {
      time_format = "%R";
    } 
    else 
    {
      time_format = "%l:%M";
    }

    float sunriseTime = calcSunRise(pblTime.tm_year, pblTime.tm_mon+1, pblTime.tm_mday, realLatitude, realLongitude, ZENITH_OFFICIAL);
    float sunsetTime = calcSunSet(pblTime.tm_year, pblTime.tm_mon+1, pblTime.tm_mday, realLatitude, realLongitude, ZENITH_OFFICIAL);
    float cDawnTime = calcSunRise(pblTime.tm_year, pblTime.tm_mon+1, pblTime.tm_mday, realLatitude, realLongitude, ZENITH_CIVIL);
    float cDuskTime = calcSunSet(pblTime.tm_year, pblTime.tm_mon+1, pblTime.tm_mday, realLatitude, realLongitude, ZENITH_CIVIL);
    float nDawnTime = calcSunRise(pblTime.tm_year, pblTime.tm_mon+1, pblTime.tm_mday, realLatitude, realLongitude, ZENITH_NAUTICAL);
    float nDuskTime = calcSunSet(pblTime.tm_year, pblTime.tm_mon+1, pblTime.tm_mday, realLatitude, realLongitude, ZENITH_NAUTICAL);
    float aDawnTime = calcSunRise(pblTime.tm_year, pblTime.tm_mon+1, pblTime.tm_mday, realLatitude, realLongitude, ZENITH_ASTRONOMICAL);
    float aDuskTime = calcSunSet(pblTime.tm_year, pblTime.tm_mon+1, pblTime.tm_mday, realLatitude, realLongitude, ZENITH_ASTRONOMICAL);
	  
    adjustTimezone(&sunriseTime);
    adjustTimezone(&sunsetTime);
	adjustTimezone(&cDawnTime);
    adjustTimezone(&cDuskTime);
	adjustTimezone(&nDawnTime);
    adjustTimezone(&nDuskTime);
	adjustTimezone(&aDawnTime);
    adjustTimezone(&aDuskTime);
    
    if (!pblTime.tm_isdst) 
    {
      sunriseTime+=1;
      sunsetTime+=1;
      cDawnTime+=1;
      cDuskTime+=1;
      nDawnTime+=1;
      nDuskTime+=1;
      aDawnTime+=1;
      aDuskTime+=1;
    } 
    
    pblTime.tm_min = (int)(60*(sunriseTime-((int)(sunriseTime))));
    pblTime.tm_hour = (int)sunriseTime;
    string_format_time(sunrise_text, sizeof(sunrise_text), time_format, &pblTime);
    text_layer_set_text(&text_sunrise_layer, sunrise_text);
    
    pblTime.tm_min = (int)(60*(sunsetTime-((int)(sunsetTime))));
    pblTime.tm_hour = (int)sunsetTime;
    string_format_time(sunset_text, sizeof(sunset_text), time_format, &pblTime);
    text_layer_set_text(&text_sunset_layer, sunset_text);
    text_layer_set_text_alignment(&text_sunset_layer, GTextAlignmentRight);

    sunriseTime+=12.0f;
    cTwilight_path_info.points[1].x = (int16_t)(my_sin(sunriseTime/24 * M_PI * 2) * 120);
    cTwilight_path_info.points[1].y = 9 - (int16_t)(my_cos(sunriseTime/24 * M_PI * 2) * 120);

    sunsetTime+=12.0f;
    cTwilight_path_info.points[4].x = (int16_t)(my_sin(sunsetTime/24 * M_PI * 2) * 120);
    cTwilight_path_info.points[4].y = 9 - (int16_t)(my_cos(sunsetTime/24 * M_PI * 2) * 120);

	cDawnTime+=12.0f;
    nTwilight_path_info.points[1].x = (int16_t)(my_sin(cDawnTime/24 * M_PI * 2) * 120);
    nTwilight_path_info.points[1].y = 9 - (int16_t)(my_cos(cDawnTime/24 * M_PI * 2) * 120);

    cDuskTime+=12.0f;
    nTwilight_path_info.points[4].x = (int16_t)(my_sin(cDuskTime/24 * M_PI * 2) * 120);
    nTwilight_path_info.points[4].y = 9 - (int16_t)(my_cos(cDuskTime/24 * M_PI * 2) * 120);
	  
	nDawnTime+=12.0f;
    aTwilight_path_info.points[1].x = (int16_t)(my_sin(nDawnTime/24 * M_PI * 2) * 120);
    aTwilight_path_info.points[1].y = 9 - (int16_t)(my_cos(nDawnTime/24 * M_PI * 2) * 120);

    nDuskTime+=12.0f;
    aTwilight_path_info.points[4].x = (int16_t)(my_sin(nDuskTime/24 * M_PI * 2) * 120);
    aTwilight_path_info.points[4].y = 9 - (int16_t)(my_cos(nDuskTime/24 * M_PI * 2) * 120);
	  
	aDawnTime+=12.0f;
    night_path_info.points[1].x = (int16_t)(my_sin(aDawnTime/24 * M_PI * 2) * 120);
    night_path_info.points[1].y = 9 - (int16_t)(my_cos(aDawnTime/24 * M_PI * 2) * 120);

    aDuskTime+=12.0f;
    night_path_info.points[4].x = (int16_t)(my_sin(aDuskTime/24 * M_PI * 2) * 120);
    night_path_info.points[4].y = 9 - (int16_t)(my_cos(aDuskTime/24 * M_PI * 2) * 120);
  
    currentData = pblTime.tm_hour;
    
    //Update location unless being called from location update
    if (!update_everything) {
      http_time_request();
    }
  }
}

//Called if Httpebble is installed on phone.
void have_time(int32_t dst_offset, bool is_dst, uint32_t unixtime, const char* tz_name, void* context) {
  if (!is_dst) {
    realTimezone = dst_offset/3600.0;
  }
  else {
    realTimezone = (dst_offset/3600.0) - 1;
  }
 
  //Now that we have timezone get location
  http_location_request();	
}

//Called if Httpebble is installed on phone.
void have_location(float latitude, float longitude, float altitude, float accuracy, void* context) {
	realLatitude = latitude;
	realLongitude = longitude;  
  
  //Update screen to reflect correct Location information
  updateDayAndNightInfo(true);
}

void handle_minute_tick(AppContextRef ctx, PebbleTickEvent *t) 
{
  (void)t;
  (void)ctx;

  // Need to be static because they're used by the system later.
  static char time_text[] = "00:00";
  static char dow_text[] = "xxx";
  	string_format_time(dow_text, sizeof(dow_text), "%a", t->tick_time);	
  static char mon_text[14];
	string_format_time(mon_text, sizeof(mon_text), "%b %e, %Y", t->tick_time);

  char *time_format;

  if (clock_is_24h_style()) 
  {
    time_format = "%R";
  } 
  else 
  {
    time_format = "%l:%M";
  }

  string_format_time(time_text, sizeof(time_text), time_format, t->tick_time);

  if (!clock_is_24h_style() && (time_text[0] == '0')) 
  {
    memmove(time_text, &time_text[1], sizeof(time_text) - 1);
  }

  text_layer_set_text(&dow_layer, dow_text);
  text_layer_set_text(&mon_layer, mon_text);
  
  text_layer_set_text(&text_time_layer, time_text);
  text_layer_set_text_alignment(&text_time_layer, GTextAlignmentCenter);

  rotbmp_pair_layer_set_angle(&bitmap_container.layer, my_max((TRIG_MAX_ANGLE * get24HourAngle(t->tick_time->tm_hour, t->tick_time->tm_min)),1.0f));
  bitmap_container.layer.layer.frame.origin.x = (144/2) - (bitmap_container.layer.layer.frame.size.w/2);
  bitmap_container.layer.layer.frame.origin.y = (168/2) + 9 - (bitmap_container.layer.layer.frame.size.h/2);

// Vibrate Every Hour
  #if HOUR_VIBRATION
	 
    if ((t->tick_time->tm_min==0) && (t->tick_time->tm_sec==0))
	{
	vibes_enqueue_custom_pattern(hour_pattern);
    }
  #endif
  
  updateDayAndNightInfo(false);
}


void handle_init(AppContextRef ctx) {
  (void)ctx;
	
  window_init(&window, "Twilight-Clock");
  window_stack_push(&window, true /* Animated */);
  window_set_background_color(&window, GColorWhite);

  resource_init_current_app(&APP_RESOURCES);
  font_moon = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_MOON_PHASES_SUBSET_30));
  layer_init(&graphics_night_layer, window.layer.frame);
  graphics_night_layer.update_proc = &graphics_night_layer_update_callback;
  layer_add_child(&window.layer, &graphics_night_layer);

  rotbmp_pair_init_container(RESOURCE_ID_IMAGE_WATCHFACE_WHITE, RESOURCE_ID_IMAGE_WATCHFACE_BLACK, &watchface_container);
  layer_add_child(&graphics_night_layer, &watchface_container.layer.layer);
  rotbmp_pair_layer_set_angle(&watchface_container.layer, 1);
  watchface_container.layer.layer.frame.origin.x = (144/2) - (watchface_container.layer.layer.frame.size.w/2);
  watchface_container.layer.layer.frame.origin.y = (168/2) - (watchface_container.layer.layer.frame.size.h/2);

  text_layer_init(&text_time_layer, window.layer.frame);
  text_layer_set_text_color(&text_time_layer, GColorBlack);
  text_layer_set_background_color(&text_time_layer, GColorClear);
  layer_set_frame(&text_time_layer.layer, GRect(0, 36, 144, 42));
  text_layer_set_font(&text_time_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_CONDENSED_42)));
  layer_add_child(&window.layer, &text_time_layer.layer);

  rotbmp_pair_init_container(RESOURCE_ID_IMAGE_HOUR_WHITE, RESOURCE_ID_IMAGE_HOUR_BLACK, &bitmap_container);
  rotbmp_pair_layer_set_src_ic(&bitmap_container.layer, GPoint(9,56));
  layer_add_child(&window.layer, &bitmap_container.layer.layer);

  text_layer_init(&moonLayer, GRect(0, 109, 144, 168-115));
  text_layer_set_text_color(&moonLayer, GColorWhite);
  text_layer_set_background_color(&moonLayer, GColorClear);
  text_layer_set_font(&moonLayer, font_moon);
  text_layer_set_text_alignment(&moonLayer, GTextAlignmentCenter);

  handle_day(ctx, NULL);
	
  layer_add_child(&window.layer, &moonLayer.layer);
	
PblTm t;
  get_time(&t);
  rotbmp_pair_layer_set_angle(&bitmap_container.layer, TRIG_MAX_ANGLE * get24HourAngle(t.tm_hour, t.tm_min));
  bitmap_container.layer.layer.frame.origin.x = (144/2) - (bitmap_container.layer.layer.frame.size.w/2);
  bitmap_container.layer.layer.frame.origin.y = (168/2) + 9 - (bitmap_container.layer.layer.frame.size.h/2);

  //Day of Week text
  text_layer_init(&dow_layer, GRect(0, 0, 144, 127+26));
  text_layer_set_text_color(&dow_layer, GColorWhite);
  text_layer_set_background_color(&dow_layer, GColorClear);
  text_layer_set_font(&dow_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_CONDENSED_19)));
  text_layer_set_text_alignment(&dow_layer, GTextAlignmentLeft);
  text_layer_set_text(&dow_layer, "xxx");
  layer_add_child(&window.layer, &dow_layer.layer);

  //Month Text
  text_layer_init(&mon_layer, GRect(0, 0, 144, 127+26));
  text_layer_set_text_color(&mon_layer, GColorWhite);
  text_layer_set_background_color(&mon_layer, GColorClear);
  text_layer_set_font(&mon_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_CONDENSED_19)));
  text_layer_set_text_alignment(&mon_layer, GTextAlignmentRight);
  text_layer_set_text(&mon_layer, "xxx");
  layer_add_child(&window.layer, &mon_layer.layer);

  //Sunrise Text 
  text_layer_init(&text_sunrise_layer, window.layer.frame);
  text_layer_set_text_color(&text_sunrise_layer, GColorWhite);
  text_layer_set_background_color(&text_sunrise_layer, GColorClear);
  layer_set_frame(&text_sunrise_layer.layer, GRect(0, 147, 144, 30));
  text_layer_set_font(&text_sunrise_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  layer_add_child(&window.layer, &text_sunrise_layer.layer);

 //Sunset Text
  text_layer_init(&text_sunset_layer, window.layer.frame);
  text_layer_set_text_color(&text_sunset_layer, GColorWhite);
  text_layer_set_background_color(&text_sunset_layer, GColorClear);
  layer_set_frame(&text_sunset_layer.layer, GRect(0, 147, 144, 30));
  text_layer_set_font(&text_sunset_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  layer_add_child(&window.layer, &text_sunset_layer.layer); 

  http_set_app_id(55122370);

  http_register_callbacks((HTTPCallbacks){
    .time=have_time,
    .location=have_location
  }, (void*)ctx);

  http_time_request();

  updateDayAndNightInfo(false);
}

void handle_deinit(AppContextRef ctx) {
  (void)ctx;

  rotbmp_pair_deinit_container(&watchface_container);
  rotbmp_pair_deinit_container(&bitmap_container);
  bmp_deinit_container(&light_grey);
  bmp_deinit_container(&grey);
  bmp_deinit_container(&dark_grey);
  fonts_unload_custom_font(font_moon);
}

void pbl_main(void *params) {
  PebbleAppHandlers handlers = {
    .init_handler = &handle_init,
    .deinit_handler = &handle_deinit,

    .tick_info = {
      .tick_handler = &handle_minute_tick,
      .tick_units = MINUTE_UNIT
    },
    .messaging_info = {
      .buffer_sizes = {
        .inbound = 124,
        .outbound = 124,
      }
    }
  };
  app_event_loop(params, &handlers);
}
