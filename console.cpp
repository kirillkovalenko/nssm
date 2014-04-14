#include "nssm.h"

/* See if we were launched from a console window. */
void check_console() {
  /* If we're running in a service context there will be no console window. */
  HWND console = GetConsoleWindow();
  if (! console) return;

  unsigned long pid;
  if (! GetWindowThreadProcessId(console, &pid)) return;

  /*
    If the process associated with the console window handle is the same as
    this process, we were not launched from an existing console.  The user
    probably double-clicked our executable.
  */
  if (GetCurrentProcessId() != pid) return;

  /* We close our new console so that subsequent messages appear in a popup. */
  FreeConsole();
}

/* Helpers for drawing the banner. */
static inline void block(unsigned int a, short x, short y, unsigned long n) {
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  TCHAR s = _T(' ');

  unsigned long out;
  COORD c = { x, y };
  FillConsoleOutputAttribute(h, a, n, c, &out);
  FillConsoleOutputCharacter(h, s, n, c, &out);
}

static inline void R(short x, short y, unsigned long n) {
  block(BACKGROUND_RED | BACKGROUND_INTENSITY, x, y, n);
}

static inline void r(short x, short y, unsigned long n) {
  block(BACKGROUND_RED, x, y, n);
}

static inline void b(short x, short y, unsigned long n) {
  block(0, x, y, n);
}

void alloc_console(nssm_service_t *service) {
  if (service->no_console) return;

  AllocConsole();

  /* Disable accidental closure. */
  HWND window = GetConsoleWindow();
  HMENU menu = GetSystemMenu(window, false);
  EnableMenuItem(menu, SC_CLOSE, MF_GRAYED);

  /* Set a title like "[NSSM] Jenkins" */
  TCHAR displayname[SERVICE_NAME_LENGTH];
  unsigned long len = _countof(displayname);
  SC_HANDLE services = open_service_manager(SC_MANAGER_CONNECT);
  if (services) {
    if (! GetServiceDisplayName(services, service->name, displayname, &len)) ZeroMemory(displayname, sizeof(displayname));
    CloseServiceHandle(services);
  }
  if (! displayname[0]) _sntprintf_s(displayname, _countof(displayname), _TRUNCATE, _T("%s"), service->name);

  TCHAR title[65535];
  _sntprintf_s(title, _countof(title), _TRUNCATE, _T("[%s] %s"), NSSM, displayname);
  SetConsoleTitle(title);

  /* Draw the NSSM logo on the console window. */
  short y = 0;

  b(0, y, 80);
  y++;

  b(0, y, 80);
  y++;

  b(0, y, 80);
  y++;

  b(0, y, 80);
  y++;

  b(0, y, 80);
  r(18, y, 5); r(28, y, 4); r(41, y, 4); r(68, y, 1);
  R(6, y, 5); R(19, y, 4); R(29, y, 1); R(32, y, 3); R(42, y, 1); R(45, y, 3); R(52, y, 5); R(69, y, 4);
  y++;

  b(0, y, 80);
  r(8, y, 4); r(20, y, 1); r(28, y, 1); r(33, y, 3); r(41, y, 1); r(46, y, 3); r (57, y, 1);
  R(9, y, 2); R(21, y, 1); R(27, y, 1); R(34, y, 1); R(40, y, 1); R(47, y, 1); R(54, y, 3); R(68, y, 3);
  y++;

  b(0, y, 80);
  r(12, y, 1); r(20, y, 1); r(26, y, 1); r(34, y, 2); r(39, y, 1); r(47, y, 2); r(67, y, 2);
  R(9, y, 3); R(21, y, 1); R(27, y, 1); R(40, y, 1); R(54, y, 1); R(56, y, 2); R(67, y, 1); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(20, y, 1); r(26, y, 1); r (35, y, 1); r(39, y, 1); r(48, y, 1); r(58, y, 1);
  R(10, y, 3); R(21, y, 1); R(27, y, 1); R(40, y, 1); R(54, y, 1); R(56, y, 2); R(67, y, 1); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(56, y, 1); r(66, y, 2);
  R(11, y, 3); R(21, y, 1); R(26, y, 2); R(39, y, 2); R(54, y, 1); R(57, y, 2); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(26, y, 1); r(39, y, 1); r(59, y, 1);
  R(12, y, 3); R(21, y, 1); R(27, y, 2); R(40, y, 2); R(54, y, 1); R(57, y, 2); R(66, y, 1); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(12, y, 4); r(30, y, 1); r(43, y, 1); r(57, y, 1); r(65, y, 2);
  R(13, y, 2); R(21, y, 1); R(27, y, 3); R(40, y, 3); R(54, y, 1); R(58, y, 2); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(13, y, 4); r(27, y, 7); r(40, y, 7);
  R(14, y, 2); R(21, y, 1); R(28, y, 5); R(41, y, 5); R(54, y, 1); R(58, y, 2); R(65, y, 1); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(60, y, 1); r(65, y, 1);
  R(14, y, 3); R(21, y, 1); R(29, y, 6); R(42, y, 6); R(54, y, 1); R(58, y, 2); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(31, y, 1); r(44, y, 1); r(58, y, 1); r(64, y, 1);
  R(15, y, 3); R(21, y, 1); R(32, y, 4); R(45, y, 4); R(54, y, 1); R(59, y, 2); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(33, y, 1); r(46, y, 1); r(61, y, 1); r(64, y, 1);
  R(16, y, 3); R(21, y, 1); R(34, y, 2); R(47, y, 2); R(54, y, 1); R(59, y, 2); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(16, y, 4); r(36, y, 1); r(49, y, 1); r(59, y, 1); r(63, y, 1);
  R(17, y, 2); R(21, y, 1); R(34, y, 2); R(47, y, 2); R(54, y, 1); R(60, y, 2); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(17, y, 4); r(26, y, 1); r(36, y, 1); r(39, y, 1); r(49, y, 1);
  R(18, y, 2); R(21, y, 1); R(35, y, 1); R(48, y, 1); R(54, y, 1); R(60, y, 2); R(63, y, 1); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(26, y, 2); r(39, y, 2); r(63, y, 1);
  R(9, y, 1); R(18, y, 4); R(35, y, 1); R(48, y, 1); R(54, y, 1); R(60, y, 3); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(34, y, 1); r(47, y, 1); r(60, y, 1);
  R(9, y, 1); R(19, y, 3); R(26, y, 2); R(35, y, 1); R(39, y, 2); R(48, y, 1); R(54, y, 1); R(61, y, 2); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(8, y, 1); r(35, y, 1); r(48, y, 1); r(62, y, 1); r(71, y, 1);
  R(9, y, 1); R(20, y, 2); R(26, y, 3); R(34, y, 1); R(39, y, 3); R(47, y, 1); R(54, y, 1); R(61, y, 1); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(11, y, 1); r(26, y, 1); r(28, y, 5); r(39, y, 1); r(41, y, 5); r(51, y, 7); r(61, y, 1); r(66, y, 8);
  R(7, y, 4); R(21, y, 1); R(29, y, 1); R(33, y, 1); R(42, y, 1); R(46, y, 1); R(52, y, 5); R(67, y, 7);
  y++;

  b(0, y, 80);
  y++;

  b(0, y, 80);
  y++;
}
