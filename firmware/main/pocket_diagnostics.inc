
bool PocketApps::Diagnostics(const std::string& command){
    std::istringstream args(command);std::string verb,action;args>>verb>>action;
    if(verb!="UI")return false;
    if(action=="HOME"){Application::GetInstance().ReturnToHome();return true;}
    if(action=="CODEX"){OpenCodex();return true;}
    if(action=="DEVICE"){OpenDeviceTools();return true;}
    if(action=="MONITOR"){OpenMonitor();return true;}
    if(action=="CALCULATOR"){OpenCalculator();return true;}
    if(action=="WOODFISH"){OpenWoodfish();return true;}
    if(action=="ANSWERBOOK"){OpenAnswerBook();return true;}
    if(action=="BACK"){Application::GetInstance().Schedule([this](){Back();});return true;}
    if(action=="SLEEP"||action=="WAKE"){
        Application::GetInstance().Schedule([action](){auto d=static_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());d->SetStandby(action=="SLEEP");});return true;
    }
    return false;
}
