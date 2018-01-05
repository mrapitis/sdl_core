/*
* Copyright (c) 2016, Ford Motor Company
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* Redistributions of source code must retain the above copyright notice, this
* list of conditions and the following disclaimer.
*
* Redistributions in binary form must reproduce the above copyright notice,
* this list of conditions and the following
* disclaimer in the documentation and/or other materials provided with the
* distribution.
*
* Neither the name of the Ford Motor Company nor the names of its contributors
* may be used to endorse or promote products derived from this software
* without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

#include <memory>
#include "application_manager/state_controller_impl.h"
#include "application_manager/usage_statistics.h"
#include "application_manager/resumption/resume_ctrl.h"
#include "utils/helpers.h"
#include "utils/make_shared.h"

namespace application_manager {

    using mobile_apis::HMILevel::eType;

    //SDL_CREATE_LOGGER("StateControllerImpl")

    bool IsStatusChanged(HmiStatePtr old_state, HmiStatePtr new_state) {
      if (old_state->hmi_level() != new_state->hmi_level() ||
          old_state->audio_streaming_state() !=
          new_state->audio_streaming_state() ||
          old_state->system_context() != new_state->system_context()) {
        return true;
      }
      return false;
    }

    StateControllerImpl::StateControllerImpl(ApplicationManager& app_mngr)
            : EventObserver(app_mngr.event_dispatcher()), app_mngr_(app_mngr) {
      subscribe_on_event(hmi_apis::FunctionID::BasicCommunication_OnAppActivated);
      subscribe_on_event(hmi_apis::FunctionID::BasicCommunication_OnAppDeactivated);
      subscribe_on_event(hmi_apis::FunctionID::TTS_Started);
      subscribe_on_event(hmi_apis::FunctionID::TTS_Stopped);
      subscribe_on_event(hmi_apis::FunctionID::VR_Started);
      subscribe_on_event(hmi_apis::FunctionID::VR_Stopped);
      subscribe_on_event(hmi_apis::FunctionID::BasicCommunication_OnEventChanged);

      conflict_states_[Application::AT_COMMON_MEDIA].push_back(
              HmiState::STATE_ID_EMBEDDED_NAVI);
      conflict_states_[Application::AT_COMMON_NAVI].push_back(
              HmiState::STATE_ID_AUDIO_SOURCE);
    }

    void StateControllerImpl::HmiLevelConflictResolver::operator()(
            ApplicationSharedPtr to_resolve) {
      DCHECK_OR_RETURN_VOID(state_ctrl_);
      if (to_resolve == applied_)
        return;
      HmiStatePtr cur_state = to_resolve->RegularHmiState();

      const bool applied_grabs_audio =
              helpers::Compare<mobile_apis::HMILevel::eType , helpers::EQ, helpers::ONE>(
                      state_->hmi_level(), mobile_apis::HMILevel::HMI_FULL, mobile_apis::HMILevel::HMI_LIMITED) &&
              applied_->is_audio();
      const bool applied_grabs_full = state_->hmi_level() == mobile_apis::HMILevel::HMI_FULL;
      const bool to_resolve_handles_full =
              cur_state->hmi_level() == mobile_apis::HMILevel::HMI_FULL;
      const bool to_resolve_handles_audio =
              helpers::Compare<mobile_apis::HMILevel::eType , helpers::EQ, helpers::ONE>(
                      cur_state->hmi_level(), mobile_apis::HMILevel::HMI_FULL, mobile_apis::HMILevel::HMI_LIMITED) &&
              to_resolve->is_audio();
      const bool same_app_type = state_ctrl_->IsSameAppType(applied_, to_resolve);

      // If applied Hmi state is FULL:
      //  all not audio applications will get BACKGROUND
      //  all applications with same HMI type will get BACKGROUND
      //  all audio applications with other HMI type(navi, vc, media) in FULL will
      //  get LIMMITED HMI level

      // If applied Hmi state is LIMITED:
      //  all applications with other HMI types will save HMI states
      //  all not audio  applications will save HMI states
      //  all applications with same HMI type will get BACKGROUND

      // If applied Hmi state is BACKGROUND:
      //  all applications will save HMI states

      mobile_apis::HMILevel::eType result_hmi_level = cur_state->hmi_level();
      if (applied_grabs_full && to_resolve_handles_audio && !same_app_type)
        result_hmi_level = mobile_apis::HMILevel::HMI_LIMITED;

      if ((applied_grabs_full && to_resolve_handles_full &&
           !to_resolve->is_audio()) ||
          (applied_grabs_audio && to_resolve_handles_audio && same_app_type))
        result_hmi_level = mobile_apis::HMILevel::HMI_BACKGROUND;

      if (cur_state->hmi_level() != result_hmi_level) {
//        SDL_LOG_DEBUG("Application " << to_resolve->app_id()
//                                     << " will change HMI level to "
//                                     << static_cast<int32_t>(result_hmi_level));
        state_ctrl_->SetupRegularHmiState(
                to_resolve,
                result_hmi_level,
                result_hmi_level == mobile_apis::HMILevel::eType::HMI_LIMITED
                ? mobile_apis::AudioStreamingState::eType::AUDIBLE
                : mobile_apis::AudioStreamingState::eType::NOT_AUDIBLE);
      } else {
//        SDL_LOG_DEBUG("Application " << to_resolve->app_id()
//                                     << " will not change HMI level");
      }
    }

    HmiStatePtr StateControllerImpl::ResolveHmiState(ApplicationSharedPtr app,
                                                     HmiStatePtr state) const {
//      SDL_LOG_AUTO_TRACE();
//      SDL_LOG_DEBUG("State to resolve: hmi_level "
//                            << static_cast<int32_t>(state->hmi_level()) << ", audio_state "
//                            << static_cast<int32_t>(state->audio_streaming_state())
//                            << ", system_context "
//                            << static_cast<int32_t>(state->system_context()));

      HmiStatePtr available_state =
              CreateHmiState(app, HmiState::StateID::STATE_ID_REGULAR);
      DCHECK_OR_RETURN(available_state, HmiStatePtr());
      available_state->set_hmi_level(state->hmi_level());
      available_state->set_audio_streaming_state(state->audio_streaming_state());
      available_state->set_system_context(state->system_context());

      if (app->is_resuming()) {
        mobile_apis::HMILevel::eType available_level = GetAvailableHmiLevel(app, state->hmi_level());
        available_state->set_hmi_level(available_level);
        available_state->set_audio_streaming_state(
                CalcAudioState(app, available_level));
      }
      return IsStateAvailable(app, available_state) ? available_state
                                                    : HmiStatePtr();
    }

    bool StateControllerImpl::IsResumptionAllowed(ApplicationSharedPtr app,
                                                  HmiStatePtr state) const {
//      SDL_LOG_AUTO_TRACE();
      if (!app->is_resuming() ||
          !helpers::Compare<mobile_apis::HMILevel::eType , helpers::EQ, helpers::ONE>(
                  state->hmi_level(), mobile_apis::HMILevel::HMI_FULL, mobile_apis::HMILevel::HMI_LIMITED)) {
//        SDL_LOG_DEBUG("Application is not in resuming mode.");
        return true;
      }

      if (IsTempStateActive(HmiState::StateID::STATE_ID_AUDIO_SOURCE) &&
          app->is_media_application()) {
//        SDL_LOG_DEBUG("Resumption for media app is not allowed. "
//                              << "AUDIO_SOURCE event is active");
        return false;
      }

      if (IsTempStateActive(HmiState::StateID::STATE_ID_EMBEDDED_NAVI) &&
          app->is_navi()) {
//        SDL_LOG_DEBUG("Resumption for navi app is not allowed. "
//                              << "EMBEDDED_NAVI event is active");
        return false;
      }

      return true;
    }

    mobile_apis::HMILevel::eType StateControllerImpl::GetAvailableHmiLevel(
            ApplicationSharedPtr app, mobile_apis::HMILevel::eType hmi_level) const {
//      SDL_LOG_AUTO_TRACE();

      mobile_apis::HMILevel::eType result = hmi_level;
      if (!helpers::Compare<mobile_apis::HMILevel::eType , helpers::EQ, helpers::ONE>(
              hmi_level, mobile_apis::HMILevel::HMI_FULL, mobile_apis::HMILevel::HMI_LIMITED)) {
        return result;
      }

      const bool is_audio_app = app->is_audio();
      const bool does_audio_app_with_same_type_exist = false;

      app_mngr_.IsAppTypeExistsInFullOrLimited(app);
      if (mobile_apis::HMILevel::HMI_LIMITED == hmi_level) {
        if (!is_audio_app || does_audio_app_with_same_type_exist) {
          result = app_mngr_.GetDefaultHmiLevel(app);
        }
        return result;
      }

      const bool is_active_app_exist = NULL != app_mngr_.active_application().get();
      if (is_audio_app) {
        if (does_audio_app_with_same_type_exist) {
          result = app_mngr_.GetDefaultHmiLevel(app);
        } else if (is_active_app_exist) {
          result = mobile_apis::HMILevel::HMI_LIMITED;
        }
      } else if (is_active_app_exist) {
        result = app_mngr_.GetDefaultHmiLevel(app);
      }

      return result;
    }

    bool StateControllerImpl::IsStateAvailable(ApplicationSharedPtr app,
                                               HmiStatePtr state) const {
//      SDL_LOG_AUTO_TRACE();
//      SDL_LOG_DEBUG("Checking state: hmi_level "
//                            << static_cast<int32_t>(state->hmi_level()) << ", audio_state "
//                            << static_cast<int32_t>(state->audio_streaming_state())
//                            << ", system_context "
//                            << static_cast<int32_t>(state->system_context()));

      if (app->is_resuming()) {
        return IsStateAvailableForResumption(app, state);
      }

      if (mobile_apis::HMILevel::HMI_FULL == state->hmi_level()) {
        if (CheckActiveStatesForConflictWithApp(app)) {
//          SDL_LOG_DEBUG(
//                  "Conflict with active state detected. Requested state is "
//                          "not available");
          return false;
        }
      }

//      SDL_LOG_DEBUG("Requested state is available");
      return true;
    }

    bool StateControllerImpl::IsStateAvailableForResumption(
            ApplicationSharedPtr app, HmiStatePtr state) const {
//      SDL_LOG_AUTO_TRACE();

      if (!app->is_resuming() ||
          !helpers::Compare<mobile_apis::HMILevel::eType , helpers::EQ, helpers::ONE>(
                  state->hmi_level(), mobile_apis::HMILevel::HMI_FULL, mobile_apis::HMILevel::HMI_LIMITED)) {
//        SDL_LOG_DEBUG("Application is not in resuming mode."
//                              << " Requested state is available");
        return true;
      }

      if (IsTempStateActive(HmiState::StateID::STATE_ID_VR_SESSION) ||
          IsTempStateActive(HmiState::StateID::STATE_ID_SAFETY_MODE) ||
          IsTempStateActive(HmiState::StateID::STATE_ID_DEACTIVATE_HMI)) {
//        SDL_LOG_DEBUG("Requested state is not available. "
//                              << "Blocked by temporary state");
        return false;
      }
      if (IsTempStateActive(HmiState::StateID::STATE_ID_PHONE_CALL) &&
          app->is_audio()) {
//        SDL_LOG_DEBUG("Requested state for audio application"
//                              << "is not available. Blocked by phone call");
        return false;
      }

//      SDL_LOG_DEBUG("Requested state is available");
      return true;
    }

    void StateControllerImpl::SetupRegularHmiState(ApplicationSharedPtr app,
                                                   HmiStatePtr state) {
      using mobile_apis::AudioStreamingState::eType;
//      SDL_LOG_AUTO_TRACE();
      DCHECK_OR_RETURN_VOID(state);
//      SDL_LOG_DEBUG("hmi_level "
//                            << static_cast<int32_t>(state->hmi_level()) << ", audio_state "
//                            << static_cast<int32_t>(state->audio_streaming_state())
//                            << ", system_context "
//                            << static_cast<int32_t>(state->system_context()));
      HmiStatePtr curr_state = app->CurrentHmiState();
      HmiStatePtr old_state =
              CreateHmiState(app, HmiState::StateID::STATE_ID_REGULAR);
      DCHECK_OR_RETURN_VOID(old_state);
      old_state->set_hmi_level(curr_state->hmi_level());
      old_state->set_audio_streaming_state(curr_state->audio_streaming_state());
      old_state->set_system_context(curr_state->system_context());

      if (mobile_apis::HMILevel::HMI_FULL == state->hmi_level()) {
        RemoveConflictedActiveStatesFromApp(app);
      }
      app->SetRegularState(state);

      if (mobile_apis::HMILevel::HMI_LIMITED == state->hmi_level() && app->is_resuming()) {
//        SDL_LOG_DEBUG("Resuming to LIMITED level. "
//                              << "Send OnResumeAudioSource notification");
        MessageHelper::SendOnResumeAudioSourceToHMI(app->app_id(), app_mngr_);
      }
      app->set_is_resuming(false);

      HmiStatePtr new_state = app->CurrentHmiState();
      OnStateChanged(app, old_state, new_state);
    }

    void StateControllerImpl::SetupRegularHmiState(
            ApplicationSharedPtr app,
            const mobile_apis::HMILevel::eType hmi_level,
            const mobile_apis::AudioStreamingState::eType audio_state) {
      using mobile_apis::HMILevel::eType ;
//      using mobile_apis::AudioStreamingState::eType;
      using helpers::Compare;
//      SDL_LOG_AUTO_TRACE();
      DCHECK_OR_RETURN_VOID(app);
      HmiStatePtr prev_state = app->RegularHmiState();
      DCHECK_OR_RETURN_VOID(prev_state);
      HmiStatePtr new_state =
              CreateHmiState(app, HmiState::StateID::STATE_ID_REGULAR);
      DCHECK_OR_RETURN_VOID(new_state);
      new_state->set_hmi_level(hmi_level);
      new_state->set_audio_streaming_state(audio_state);
      new_state->set_system_context(prev_state->system_context());
      SetupRegularHmiState(app, new_state);
    }

    void StateControllerImpl::ApplyRegularState(ApplicationSharedPtr app,
                                                HmiStatePtr state) {
//      SDL_LOG_AUTO_TRACE();
      DCHECK_OR_RETURN_VOID(app);
      DCHECK_OR_RETURN_VOID(state);
      DCHECK_OR_RETURN_VOID(state->state_id() == HmiState::STATE_ID_REGULAR);
      SetupRegularHmiState(app, state);
      ForEachApplication<HmiLevelConflictResolver>(
              HmiLevelConflictResolver(app, state, this));
    }

    bool StateControllerImpl::IsSameAppType(ApplicationConstSharedPtr app1,
                                            ApplicationConstSharedPtr app2) {
      const bool both_media = app1->is_media_application() && app2->is_media_application();
      const bool both_navi = app1->is_navi() && app2->is_navi();
      const bool both_vc =
              app1->is_voice_communication_supported() && app2->is_voice_communication_supported();
      const bool both_simple = !app1->is_audio() && !app2->is_audio();
      return both_simple || both_media || both_navi || both_vc;
    }

    void StateControllerImpl::on_event(const event_engine::Event& event) {
      using smart_objects::SmartObject;
      using event_engine::Event;
      using hmi_apis::FunctionID::eType;

//      SDL_LOG_AUTO_TRACE();
      const SmartObject& message = event.smart_object();
      const hmi_apis::FunctionID::eType id = static_cast<hmi_apis::FunctionID::eType >(event.id());
      switch (id) {
        case hmi_apis::FunctionID::BasicCommunication_ActivateApp: {
          OnActivateAppResponse(message);
          break;
        }
        case hmi_apis::FunctionID::BasicCommunication_OnAppActivated: {
          OnAppActivated(message);
          break;
        }
        case hmi_apis::FunctionID::BasicCommunication_OnAppDeactivated: {
          OnAppDeactivated(message);
          break;
        }
        case hmi_apis::FunctionID::VR_Started: {
          ApplyTempState<HmiState::STATE_ID_VR_SESSION>();
          break;
        }
        case hmi_apis::FunctionID::VR_Stopped: {
          CancelTempState<HmiState::STATE_ID_VR_SESSION>();
          break;
        }
        case hmi_apis::FunctionID::TTS_Started: {
          ApplyTempState<HmiState::STATE_ID_TTS_SESSION>();
          break;
        }
        case hmi_apis::FunctionID::TTS_Stopped: {
          CancelTempState<HmiState::STATE_ID_TTS_SESSION>();
          break;
        }
        case hmi_apis::FunctionID::BasicCommunication_OnEventChanged: {
          bool is_active =
                  message[strings::msg_params][hmi_notification::is_active].asBool();
          const uint32_t id =
                  message[strings::msg_params][hmi_notification::event_name].asUInt();
          // TODO(AOleynik): Add verification/conversion check here
          hmi_apis::Common_EventTypes::eType state_id =
                  static_cast<hmi_apis::Common_EventTypes::eType >(id);
          if (is_active) {
            if (hmi_apis::Common_EventTypes::AUDIO_SOURCE == state_id) {
              ApplyTempState<HmiState::STATE_ID_AUDIO_SOURCE>();
              break;
            }
            if (hmi_apis::Common_EventTypes::EMBEDDED_NAVI == state_id) {
              ApplyTempState<HmiState::STATE_ID_EMBEDDED_NAVI>();
              break;
            }
            if (hmi_apis::Common_EventTypes::PHONE_CALL == state_id) {
              ApplyTempState<HmiState::STATE_ID_PHONE_CALL>();
              break;
            }
            if (hmi_apis::Common_EventTypes::EMERGENCY_EVENT == state_id) {
              ApplyTempState<HmiState::STATE_ID_SAFETY_MODE>();
              break;
            }
            if (hmi_apis::Common_EventTypes::DEACTIVATE_HMI == state_id) {
              resumption::ResumeCtrl& resume_ctrl = app_mngr_.resume_controller();
              resume_ctrl.PostponeHmiLevelResumption();
              ApplyTempState<HmiState::STATE_ID_DEACTIVATE_HMI>();
              break;
            }
          } else {
            if (hmi_apis::Common_EventTypes::AUDIO_SOURCE == state_id) {
              CancelTempState<HmiState::STATE_ID_AUDIO_SOURCE>();
              break;
            }
            if (hmi_apis::Common_EventTypes::EMBEDDED_NAVI == state_id) {
              CancelTempState<HmiState::STATE_ID_EMBEDDED_NAVI>();
              break;
            }
            if (hmi_apis::Common_EventTypes::PHONE_CALL == state_id) {
              CancelTempState<HmiState::STATE_ID_PHONE_CALL>();
              break;
            }
            if (hmi_apis::Common_EventTypes::EMERGENCY_EVENT == state_id) {
              CancelTempState<HmiState::STATE_ID_SAFETY_MODE>();
              break;
            }
            if (hmi_apis::Common_EventTypes::DEACTIVATE_HMI == state_id) {
              CancelTempState<HmiState::STATE_ID_DEACTIVATE_HMI>();
              resumption::ResumeCtrl& resume_ctrl = app_mngr_.resume_controller();
              resume_ctrl.RestoreHmiLevelResumption();
              break;
            }
          }
          break;
        }
        default:
          break;
      }
    }

    void StateControllerImpl::OnStateChanged(ApplicationSharedPtr app,
                                             HmiStatePtr old_state,
                                             HmiStatePtr new_state) {
//      SDL_LOG_AUTO_TRACE();
      DCHECK_OR_RETURN_VOID(app);
      DCHECK_OR_RETURN_VOID(old_state);
      DCHECK_OR_RETURN_VOID(new_state);
//      SDL_LOG_DEBUG("old: hmi_level "
//                            << static_cast<int32_t>(old_state->hmi_level())
//                            << ", audio_state "
//                            << static_cast<int32_t>(old_state->audio_streaming_state())
//                            << ", system_context "
//                            << static_cast<int32_t>(old_state->system_context()));
//      SDL_LOG_DEBUG("new: hmi_level "
//                            << static_cast<int32_t>(new_state->hmi_level())
//                            << ", audio_state "
//                            << static_cast<int32_t>(new_state->audio_streaming_state())
//                            << ", system_context "
//                            << static_cast<int32_t>(new_state->system_context()));
      if (IsStatusChanged(old_state, new_state)) {
        app_mngr_.SendHMIStatusNotification(app);
        if (new_state->hmi_level() == mobile_apis::HMILevel::HMI_NONE) {
          app->ResetDataInNone();
        }
        app_mngr_.OnHMILevelChanged(
                app->app_id(), old_state->hmi_level(), new_state->hmi_level());
        app->usage_report().RecordHmiStateChanged(new_state->hmi_level());
      } else {
//        SDL_LOG_ERROR("Status not changed");
      }
    }

    bool StateControllerImpl::IsTempStateActive(HmiState::StateID ID) const {
      sync_primitives::AutoLock autolock(active_states_lock_);
      StateIDList::const_iterator itr =
              std::find(active_states_.begin(), active_states_.end(), ID);
      return active_states_.end() != itr;
    }

    void StateControllerImpl::OnApplicationRegistered(
            ApplicationSharedPtr app, const mobile_apis::HMILevel::eType default_level) {
//      using mobile_apis::HMILevel::eType;
//      using mobile_apis::AudioStreamingState::eType;
//      using mobile_apis::SystemContext::eType;
//      SDL_LOG_AUTO_TRACE();
      DCHECK_OR_RETURN_VOID(app);

      active_states_lock_.Acquire();
      StateIDList::iterator it = active_states_.begin();
      for (; it != active_states_.end(); ++it) {
        HmiStatePtr new_state = CreateHmiState(app, *it);
        DCHECK_OR_RETURN_VOID(new_state);
        DCHECK_OR_RETURN_VOID(new_state->state_id() != HmiState::STATE_ID_REGULAR);
        HmiStatePtr old_hmi_state = app->CurrentHmiState();
        new_state->set_parent(old_hmi_state);
        app->AddHMIState(new_state);
      }
      active_states_lock_.Release();

      HmiStatePtr default_state =
              CreateHmiState(app, HmiState::StateID::STATE_ID_REGULAR);
      DCHECK_OR_RETURN_VOID(default_state);
      default_state->set_hmi_level(default_level);
      default_state->set_audio_streaming_state(CalcAudioState(app, default_level));
      default_state->set_system_context(mobile_apis::SystemContext::SYSCTXT_MAIN);

      HmiStatePtr initial_state = app->RegularHmiState();

      app->SetRegularState(default_state);

      HmiStatePtr new_state = app->CurrentHmiState();

      OnStateChanged(app, initial_state, new_state);
    }

    int64_t StateControllerImpl::SendBCActivateApp(ApplicationConstSharedPtr app,
                                                   hmi_apis::Common_HMILevel::eType level,
                                                   bool send_policy_priority) {
//      SDL_LOG_AUTO_TRACE();
      smart_objects::SmartObjectSPtr bc_activate_app_request =
              MessageHelper::GetBCActivateAppRequestToHMI(
                      app,
                      app_mngr_.connection_handler().get_session_observer(),
                      app_mngr_.GetPolicyHandler(),
                      level,
                      send_policy_priority,
                      app_mngr_);
      if (!bc_activate_app_request) {
//        SDL_LOG_ERROR("Unable to create BC.ActivateAppRequest");
        return -1;
      }
      if (!app_mngr_.ManageHMICommand(bc_activate_app_request)) {
//        SDL_LOG_ERROR("Unable to send BC.ActivateAppRequest");
        return -1;
      }
      const int64_t corr_id =
              (*bc_activate_app_request)[strings::params][strings::correlation_id]
                      .asInt();
      return corr_id;
    }

    void StateControllerImpl::OnTempStateStartedForApp(HmiState::StateID ID,
                                                       ApplicationSharedPtr app) {
//      SDL_LOG_AUTO_TRACE();
      if (HmiState::STATE_ID_DEACTIVATE_HMI == ID) {
        app->set_keeping_hmi_level(true);
//        SDL_LOG_DEBUG("Enabled keeping hmi level for app " << app->app_id());
      }
    }

    void StateControllerImpl::OnTempStateStoppedForApp(HmiState::StateID ID,
                                                       ApplicationSharedPtr app) {
//      SDL_LOG_AUTO_TRACE();
      HmiStatePtr state = app->PostponedHmiState();
      if (state) {
        app->RemovePostponedState();
        state->set_state_id(HmiState::STATE_ID_REGULAR);
        SetRegularState(app, state);
      }

      if (HmiState::STATE_ID_DEACTIVATE_HMI == ID) {
        app->set_keeping_hmi_level(false);
//        SDL_LOG_DEBUG("Disabled keeping hmi level for app" << app->app_id());
      }
    }

    void StateControllerImpl::TempStateStarted(HmiState::StateID ID) {
//      SDL_LOG_AUTO_TRACE();
      sync_primitives::AutoLock autolock(active_states_lock_);
      StateIDList::iterator it =
              std::find(active_states_.begin(), active_states_.end(), ID);
      if (it == active_states_.end()) {
        active_states_.push_back(ID);
      } else {
//        SDL_LOG_ERROR("StateID " << ID << " is already active");
      }

      ForEachApplicationWithParam(&StateControllerImpl::OnTempStateStartedForApp,
                                  ID);
    }

    void StateControllerImpl::TempStateStopped(HmiState::StateID ID) {
//      SDL_LOG_AUTO_TRACE();
      {
        sync_primitives::AutoLock autolock(active_states_lock_);
        active_states_.remove(ID);
      }

      ForEachApplicationWithParam(&StateControllerImpl::OnTempStateStoppedForApp,
                                  ID);
    }

    void StateControllerImpl::DeactivateApp(ApplicationSharedPtr app) {
//      SDL_LOG_AUTO_TRACE();

      DCHECK_OR_RETURN_VOID(app);
      HmiStatePtr regular = app->RegularHmiState();
      DCHECK_OR_RETURN_VOID(regular);
      HmiStatePtr new_regular =  utils::MakeShared<HmiState>(*regular);

      if (app->is_audio()) {
        new_regular->set_hmi_level(mobile_apis::HMILevel::HMI_LIMITED);
        new_regular->set_audio_streaming_state(
                mobile_apis::AudioStreamingState::AUDIBLE);
      } else {
        new_regular->set_hmi_level(mobile_apis::HMILevel::HMI_BACKGROUND);
        new_regular->set_audio_streaming_state(
                mobile_apis::AudioStreamingState::NOT_AUDIBLE);
      }

      SetRegularState(app, new_regular, false);
    }

    void StateControllerImpl::OnActivateAppResponse(
            const smart_objects::SmartObject& message) {
//      SDL_LOG_AUTO_TRACE();
      const hmi_apis::Common_Result::eType code = static_cast<hmi_apis::Common_Result::eType>(
              message[strings::params][hmi_response::code].asInt());
      const int32_t correlation_id =
              message[strings::params][strings::correlation_id].asInt();
      const uint32_t hmi_app_id = app_mngr_.application_id(correlation_id);
      ApplicationSharedPtr application =
              app_mngr_.application_by_hmi_app(hmi_app_id);

      if (application && hmi_apis::Common_Result::SUCCESS == code) {
        lock_.Acquire();
        HmiStatePtr pending_state = waiting_for_activate[hmi_app_id];
        lock_.Release();
        DCHECK_OR_RETURN_VOID(pending_state);
        ApplyRegularState(application, pending_state);
      }
      lock_.Acquire();
      waiting_for_activate.erase(hmi_app_id);
      lock_.Release();
      if (!waiting_for_applying_state_.empty() && waiting_for_activate.empty()) {
        ProcessSavingHMIState();
      }
    }

    void StateControllerImpl::OnAppActivated(
            const smart_objects::SmartObject& message) {
//      SDL_LOG_AUTO_TRACE();

      uint32_t app_id = message[strings::msg_params][strings::app_id].asUInt();
      ApplicationSharedPtr app = app_mngr_.application(app_id);
      if (!app) {
//        SDL_LOG_ERROR("Application with id " << app_id << " not found");
        return;
      }

      SetRegularState(app, mobile_apis::HMILevel::HMI_FULL, true);
    }

    void StateControllerImpl::OnAppDeactivated(
            const smart_objects::SmartObject& message) {
//      SDL_LOG_AUTO_TRACE();

      uint32_t app_id = message[strings::msg_params][strings::app_id].asUInt();
      ApplicationSharedPtr app = app_mngr_.application(app_id);
      if (!app) {
//        SDL_LOG_ERROR("Application with id " << app_id << " not found");
        return;
      }

      if (mobile_apis::HMILevel::HMI_FULL != app->hmi_level()) {
        return;
      }

      // TODO(AOleynik): Need to delete DeactivateReason and modify OnAppDeactivated
      // when HMI will support that, otherwise won't be testable
      DeactivateApp(app);
    }

    void StateControllerImpl::OnNaviStreamingStarted() {
      ApplyTempState<HmiState::STATE_ID_NAVI_STREAMING>();
    }

    void StateControllerImpl::OnNaviStreamingStopped() {
      CancelTempState<HmiState::STATE_ID_NAVI_STREAMING>();
    }

    bool StateControllerImpl::IsStateActive(HmiState::StateID state_id) const {
//      SDL_LOG_AUTO_TRACE();
      switch (state_id) {
        case HmiState::STATE_ID_CURRENT:
        case HmiState::STATE_ID_REGULAR:
          return true;
        default:
          return IsTempStateActive(state_id);
      }

      return false;
    }

    void StateControllerImpl::OnTimeOutActivateAppRequest(uint32_t hmi_app_id) {
//      SDL_LOG_AUTO_TRACE();
//      SDL_LOG_INFO(
//              "TimeOut was received for"
//                      "application with hmi_app_id = "
//                      << hmi_app_id);
      lock_.Acquire();
      waiting_for_activate.erase(hmi_app_id);
      lock_.Release();
      if (!waiting_for_applying_state_.empty() && waiting_for_activate.empty()) {
        ProcessSavingHMIState();
      }
    }

    void StateControllerImpl::OnAppStateChanged(ApplicationSharedPtr app,
                                                StateChangeReason reason) {
//      SDL_LOG_AUTO_TRACE();
      if (IsTempStateActive(HmiState::StateID::STATE_ID_PHONE_CALL)) {
        mobile_apis::HMILevel::eType old_hmi_level = app->CurrentHmiState()->hmi_level();
        app->SetAppState(HmiState::StateID::STATE_ID_PHONE_CALL, reason);
        mobile_apis::HMILevel::eType new_hmi_level = app->CurrentHmiState()->hmi_level();
        if (old_hmi_level != new_hmi_level) {
          app_mngr_.SendHMIStatusNotification(app);
          app_mngr_.OnHMILevelChanged(app->app_id(), old_hmi_level, new_hmi_level);
          app->usage_report().RecordHmiStateChanged(new_hmi_level);
        }
      }
    }

    HmiStatePtr StateControllerImpl::CreateHmiState(
            ApplicationSharedPtr app, HmiState::StateID state_id) const {
//      SDL_LOG_AUTO_TRACE();
      HmiStatePtr new_state;
      switch (state_id) {
        case HmiState::STATE_ID_PHONE_CALL: {
          new_state = utils::MakeShared<PhoneCallHmiState>(app, app_mngr_);
          break;
        }
        case HmiState::STATE_ID_SAFETY_MODE: {
          new_state = utils::MakeShared<SafetyModeHmiState>(app, app_mngr_);
          break;
        }
        case HmiState::STATE_ID_VR_SESSION: {
          new_state = utils::MakeShared<VRHmiState>(app, app_mngr_);
          break;
        }
        case HmiState::STATE_ID_TTS_SESSION: {
          new_state = utils::MakeShared<TTSHmiState>(app, app_mngr_);
          break;
        }
        case HmiState::STATE_ID_NAVI_STREAMING: {
          new_state = utils::MakeShared<NaviStreamingHmiState>(app, app_mngr_);
          break;
        }
        case HmiState::STATE_ID_REGULAR: {
          new_state = utils::MakeShared<HmiState>(app, app_mngr_);
          break;
        }
        case HmiState::STATE_ID_POSTPONED: {
          new_state = utils::MakeShared<HmiState>(app, app_mngr_, state_id);
          break;
        }
        case HmiState::STATE_ID_DEACTIVATE_HMI: {
          new_state = utils::MakeShared<DeactivateHMI>(app, app_mngr_);
          break;
        }
        case HmiState::STATE_ID_AUDIO_SOURCE: {
          new_state = utils::MakeShared<AudioSource>(app, app_mngr_);
          break;
        }
        case HmiState::STATE_ID_EMBEDDED_NAVI: {
          new_state = utils::MakeShared<EmbeddedNavi>(app, app_mngr_);
          break;
        }
        default:
//          SDL_LOG_FATAL("Invalid state_id " << state_id);
          NOTREACHED();
              break;
      }
      return new_state;
    }

    mobile_apis::AudioStreamingState::eType StateControllerImpl::CalcAudioState(
            ApplicationSharedPtr app, const mobile_apis::HMILevel::eType hmi_level) const {
      using mobile_apis::HMILevel::eType;
//      using mobile_apis::AudioStreamingState::eType;
      using helpers::Compare;
      using helpers::EQ;
      using helpers::ONE;

      mobile_apis::AudioStreamingState::eType audio_state = mobile_apis::AudioStreamingState::NOT_AUDIBLE;
      if (Compare<mobile_apis::HMILevel::eType , EQ, ONE>(
              hmi_level, mobile_apis::HMILevel::HMI_FULL, mobile_apis::HMILevel::HMI_LIMITED)) {
        if (app->is_audio()) {
          audio_state = mobile_apis::AudioStreamingState::AUDIBLE;
        }
      }
      return audio_state;
    }

    void StateControllerImpl::ProcessSavingHMIState() {
//      SDL_LOG_AUTO_TRACE();
      bool send_activate_app;
      HmiStatePtr state;
      ApplicationSharedPtr app;

      while (waiting_for_activate.empty()) {
        {
          sync_primitives::AutoLock auto_lock(lock_);
          std::vector<DataForActivation>::iterator it =
                  waiting_for_applying_state_.begin();
          if (it == waiting_for_applying_state_.end()) {
//            SDL_LOG_WARN("Doesn't contain any hmi state");
            return;
          }
          app = app_mngr_.application((it->app_id_));
          if (!app) {
//            SDL_LOG_WARN("Application with appID = " << it->app_id_
//                                                     << " does not exist");
            waiting_for_applying_state_.erase(it);
            continue;
          }
//          SDL_LOG_INFO("Application with appID = " << it->app_id_ << "exist");
          send_activate_app = it->send_activate_app_;
          state = it->state_;
          waiting_for_applying_state_.erase(it);
        }
        SetRegularState(app, state, send_activate_app);
      }
    }

    bool StateControllerImpl::CheckActiveStatesForConflictWithApp(
            ApplicationConstSharedPtr app) const {
      ConflictStatesMap::const_iterator type_it = conflict_states_.begin();
      for (; conflict_states_.end() != type_it; ++type_it) {
        if (!app->is_app_type_included(type_it->first)) {
          const std::vector<HmiState::StateID>& states = type_it->second;
          if (states.end() !=
              std::find_if(
                      states.begin(),
                      states.end(),
                      std::bind1st(
                              std::mem_fun(&StateControllerImpl::IsTempStateActive),
                              this))) {
            return true;
          }
        }
      }
      return false;
    }

    void StateControllerImpl::RemoveConflictedActiveStatesFromApp(
            ApplicationSharedPtr app) const {
      ConflictStatesMap::const_iterator type_it = conflict_states_.begin();
      for (; conflict_states_.end() != type_it; ++type_it) {
        if (app->is_app_type_included(type_it->first)) {
          const std::vector<HmiState::StateID>& states = type_it->second;

          std::vector<HmiState::StateID>::const_iterator state_it = states.begin();
          for (;;) {
            state_it = std::find_if(
                    state_it,
                    states.end(),
                    std::bind1st(std::mem_fun(&StateControllerImpl::IsTempStateActive),
                                 this));

            if (states.end() == state_it) {
              break;
            }
            app->RemoveHMIState(*state_it++);
          }
        }
      }
    }

}  // namespace application_manager