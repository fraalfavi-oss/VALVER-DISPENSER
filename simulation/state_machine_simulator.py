from enum import Enum, auto

class SystemState(Enum):
    BOOT = auto()
    INITIALIZING = auto()
    IDLE = auto()
    NOT_HOMED = auto()
    HOMING = auto()
    READY = auto()
    DISPENSING = auto()
    PAUSED = auto()
    FAULT = auto()
    DISABLED = auto()

class SimulatedController:
    def __init__(self):
        self.state = SystemState.BOOT
        self.motors_enabled = False
        self.fault_active = False

    def initialize(self):
        self.state = SystemState.INITIALIZING
        self.motors_enabled = False
        self.fault_active = False
        self.state = SystemState.IDLE

    def command_move(self, authorized):
        if self.fault_active or not authorized:
            return False
        if not self.motors_enabled:
            return False
        self.state = SystemState.DISPENSING
        return True

    def enable_motors(self, allowed):
        self.motors_enabled = allowed and not self.fault_active

    def fault(self):
        self.fault_active = True
        self.state = SystemState.FAULT

    def clear_fault(self):
        self.fault_active = False
        self.state = SystemState.IDLE

if __name__ == "__main__":
    c = SimulatedController()
    c.initialize()
    assert c.state == SystemState.IDLE
    assert c.command_move(False) is False
    c.enable_motors(False)
    assert c.command_move(True) is False
    c.enable_motors(True)
    assert c.command_move(True) is True
    c.fault()
    assert c.command_move(True) is False
    c.clear_fault()
    print("Simulation OK: state machine and command validation are working.")
