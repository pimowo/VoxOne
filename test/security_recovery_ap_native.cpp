#include "../src/core/security_recovery_ap.h"
#include "../src/core/security_recovery_http.h"
#include <cassert>
#include <cstring>
using namespace voxone::security_recovery;
namespace {
class Session:public RecoveryApSessionControl { public: RecoveryState value=RecoveryState::Requested; bool live()const override{return value==RecoveryState::Requested||value==RecoveryState::Active||value==RecoveryState::Committing;} RecoveryState state()const override{return value;} RecoveryTransition activate()override{auto from=value;value=RecoveryState::Active;return{from,value,RecoveryDiagnostic::Activated,true};} RecoveryTransition fail()override{auto from=value;value=RecoveryState::Failed;return{from,value,RecoveryDiagnostic::InternalFailure,true};} RecoveryTransition complete(bool ok)override{auto from=value;value=ok?RecoveryState::Completed:RecoveryState::Failed;return{from,value,ok?RecoveryDiagnostic::Completed:RecoveryDiagnostic::CommitFailed,true};}};
class Platform:public RecoveryApPlatform { public: bool wifi=true,http=true,serving=true,restarted=false; bool startWifi()override{return wifi;} bool startHttp()override{return http;} bool serviceHttp(uint32_t)override{return serving;} void stopHttp()override{} void disconnectClient()override{} void stopWifi()override{} void clearTemporary()override{} void restart()override{restarted=true;} };
RecoveryHttpDecision decision(const char* request) { RecoveryHttpRequest parsed{}; return evaluateRecoveryHttpRequest(request,std::strlen(request),"192.168.4.1",true,parsed); }
}
int main(){
  Session session; Platform platform; RecoveryApController controller(session,platform); assert(controller.start(0)==RecoveryApStartResult::Active); assert(controller.running()&&controller.ownsRuntime()); controller.complete(true); assert(platform.restarted);
  assert(decision("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n")==RecoveryHttpDecision::Root);
  const char password[]="POST /password HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 3\r\n\r\na=b";
  assert(decision(password)==RecoveryHttpDecision::Password);
  const char factory[]="POST /factory-reset HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 3\r\n\r\na=b";
  assert(decision(factory)==RecoveryHttpDecision::FactoryReset);
  assert(decision("GET /api/ready HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n")==RecoveryHttpDecision::NotFound);
}
