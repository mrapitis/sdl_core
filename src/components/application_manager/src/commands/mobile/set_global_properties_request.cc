/*

 Copyright (c) 2013, Ford Motor Company
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.

 Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following
 disclaimer in the documentation and/or other materials provided with the
 distribution.

 Neither the name of the Ford Motor Company nor the names of its contributors
 may be used to endorse or promote products derived from this software
 without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
 */

#include "application_manager/commands/mobile/set_global_properties_request.h"
#include "application_manager/application_manager_impl.h"
#include "application_manager/application_impl.h"
#include "application_manager/message_helper.h"
#include "interfaces/MOBILE_API.h"
#include "interfaces/HMI_API.h"

namespace application_manager {

namespace commands {

SetGlobalPropertiesRequest::SetGlobalPropertiesRequest(
  const MessageSharedPtr& message): CommandRequestImpl(message) {
}

SetGlobalPropertiesRequest::~SetGlobalPropertiesRequest() {
}

void SetGlobalPropertiesRequest::Run() {
  LOG4CXX_INFO(logger_, "SetGlobalPropertiesRequest::Run");

  int app_id = (*message_)[strings::params][strings::connection_key];
  Application* app = ApplicationManagerImpl::instance()->application(app_id);

  if (NULL == app) {
    LOG4CXX_ERROR_EXT(logger_, "No application associated with session key");
    SendResponse(false, mobile_apis::Result::APPLICATION_NOT_REGISTERED);
    return;
  }

  if (!MessageHelper::VerifyImageFiles((*message_)[strings::msg_params], app)) {
    LOG4CXX_ERROR_EXT(logger_, "INVALID_DATA");
    SendResponse(false, mobile_apis::Result::INVALID_DATA);
    return;
  }

  // by default counter is 1 for TTS request. If only one param specified
  // for TTS REJECT response will be sent
  unsigned int chaining_counter = 1;
  if ((*message_)[strings::msg_params].keyExists(strings::help_prompt) &&
      (*message_)[strings::msg_params].keyExists(strings::timeout_prompt)) {
    ++chaining_counter;
  }

  if ((*message_)[strings::msg_params].keyExists(strings::vr_help_title) &&
      (*message_)[strings::msg_params].keyExists(strings::vr_help)) {

    // check vrhelpitem position index
    if (!CheckVrHelpItemsOrder()) {
      LOG4CXX_ERROR(logger_, "Request rejected");
      SendResponse(false, mobile_apis::Result::REJECTED);
      return;
    }

    app->set_vr_help_title(
        (*message_)[strings::msg_params].getElement(strings::vr_help_title));
    app->set_vr_help(
        (*message_)[strings::msg_params].getElement(strings::vr_help));

    smart_objects::SmartObject msg_params =
      smart_objects::SmartObject(smart_objects::SmartType_Map);

    msg_params[strings::vr_help_title] = (*app->vr_help_title());
    msg_params[strings::vr_help] = (*app->vr_help());
    msg_params[strings::app_id] = app->app_id();

    CreateHMIRequest(hmi_apis::FunctionID::UI_SetGlobalProperties,
                     msg_params, true, chaining_counter);
  } else if (
      !(*message_)[strings::msg_params].keyExists(strings::vr_help_title) &&
      !(*message_)[strings::msg_params].keyExists(strings::vr_help)) {

      const CommandsMap& cmdMap = app->commands_map();
      CommandsMap::const_iterator command_it = cmdMap.begin();

      int index = 0;
      smart_objects::SmartObject vr_help_items;
      for (; cmdMap.end() != command_it; ++command_it) {
        if (false == (*command_it->second).keyExists(strings::vr_commands)) {
          LOG4CXX_ERROR(logger_, "VR synonyms are empty");
          SendResponse(false, mobile_apis::Result::INVALID_DATA);
          return;
        }
        // use only first
        vr_help_items[index++] = (*command_it->second)[strings::vr_commands][0];
      }

      app->set_vr_help_title(smart_objects::SmartObject(app->name()));
      app->set_vr_help(vr_help_items);

      smart_objects::SmartObject msg_params =
        smart_objects::SmartObject(smart_objects::SmartType_Map);

      msg_params[strings::vr_help_title] = (*app->vr_help_title());
      msg_params[strings::vr_help] = (*app->vr_help());
      msg_params[strings::app_id] = app->app_id();

      CreateHMIRequest(hmi_apis::FunctionID::UI_SetGlobalProperties,
                       msg_params, true, chaining_counter);
  } else {
      LOG4CXX_ERROR(logger_, "Request rejected");
      SendResponse(false, mobile_apis::Result::REJECTED);
      return;
  }

  // check TTS params
  if ((*message_)[strings::msg_params].keyExists(strings::help_prompt) &&
      (*message_)[strings::msg_params].keyExists(strings::timeout_prompt)) {

    app->set_help_prompt(
        (*message_)[strings::msg_params].getElement(strings::help_promt));
    app->set_timeout_prompt(
      (*message_)[strings::msg_params].getElement(strings::timeout_promt));

    smart_objects::SmartObject msg_params =
      smart_objects::SmartObject(smart_objects::SmartType_Map);

    msg_params[strings::help_prompt] = (*app->help_promt());
    msg_params[strings::timeout_prompt] = (*app->timeout_promt());
    msg_params[strings::app_id] = app->app_id();

    CreateHMIRequest(hmi_apis::FunctionID::TTS_SetGlobalProperties,
                     msg_params, true);
  }
}

bool SetGlobalPropertiesRequest::CheckVrHelpItemsOrder() {
  const smart_objects::SmartObject vr_help =
      (*message_)[strings::msg_params].getElement(strings::vr_help);

  // Check if VR Help Items contains sequential positionss
  size_t i = 0;
  for (size_t j = 1; j < vr_help.length(); ++i, ++j) {
    if ((vr_help.getElement(i).getElement(strings::position).asInt() + 1) !=
        vr_help.getElement(j).getElement(strings::position).asInt()) {
      LOG4CXX_ERROR(logger_, "VR help items order is wrong");
      return false;
    }
  }

  return true;
}

}  // namespace commands

}  // namespace application_manager
