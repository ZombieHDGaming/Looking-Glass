/*
OBS Looking Glass - Custom Dynamic Multiview Plugin
Copyright (C) 2025

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

/**
 * Exposes the multiview window controls through obs-websocket's vendor API so
 * they can be driven from outside OBS Studio (stream decks, companion, custom
 * tooling) in addition to the Tools -> Looking Glass submenus.
 *
 * Vendor name: "looking-glass"
 * Requests:
 *   OpenMultiview               { "multiviewName": string }
 *   CloseMultiview              { "multiviewName": string }
 *   SendMultiviewToMainDisplay  { "multiviewName": string }
 *
 * Every response carries a "success" boolean, plus an "error" string when the
 * request could not be carried out.
 */
namespace LookingGlassWebSocket {

// Registers the vendor and its requests. Must be called from
// obs_module_post_load() so that obs-websocket is guaranteed to be loaded.
void Register();

// Unregisters the requests. Call while obs-websocket is still loaded
// (i.e. on OBS_FRONTEND_EVENT_EXIT), never after module teardown has begun.
void Unregister();

} // namespace LookingGlassWebSocket
