#include "symbiose_ircd.h"

int SymbioseIrcd_Init(uint16_t port) { return 0; }
int SymbioseIrcd_Run(void) { return 0; }
void SymbioseIrcd_Shutdown(void) {}
PSYMBIOSE_CLIENT SymbioseIrcd_AcceptClient(int server_fd) { return NULL; }
void SymbioseIrcd_DisconnectClient(PSYMBIOSE_CLIENT client) {}
int SymbioseIrcd_SendJumbo(PSYMBIOSE_CLIENT client, SYMBIOSE_MSG_TYPE type,
                           const uint8_t *payload, size_t payload_len) {
  return 0;
}
int SymbioseIrcd_RecvJumbo(PSYMBIOSE_CLIENT client, SYMBIOSE_MSG_TYPE *type,
                           uint8_t **payload, size_t *payload_len) {
  return 0;
}
int SymbioseIrcd_DispatchTask(PSYMBIOSE_CLIENT oracle, PSYMBIOSE_CLIENT scout,
                              const char *task_description, size_t desc_len) {
  return 0;
}
int SymbioseIrcd_ReportResult(PSYMBIOSE_CLIENT scout, PSYMBIOSE_CLIENT oracle,
                              const char *result, size_t result_len) {
  return 0;
}

int main() {
  SymbioseIrcd_Init(6667);
  SymbioseIrcd_Run();
  return 0;
}
