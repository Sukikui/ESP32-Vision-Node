#pragma once

/* Parse an incoming MQTT command and dispatch it to the matching handler. */
void command_router_handle(const char *topic, int topic_len, const char *data, int data_len);
