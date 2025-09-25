#!/usr/bin/env python3

from PyQt5.QtWidgets import QMenu, QApplication, QPushButton, QWidget, QGridLayout
from PyQt5.QtGui import QCursor, QGuiApplication
from PyQt5.QtCore import Qt, QTimer, QEvent, QPoint
from wayfire.core.template import get_msg_template
from wayfire import WayfireSocket
import sys

sock = WayfireSocket()

class MyWidget(QMenu):
    def __init__(self):
        super().__init__()
        self.layout = QGridLayout(self)
        self.setWindowTitle('wf-menu')
        self.setWindowFlags(Qt.FramelessWindowHint)
        self.workspace_submenu = QMenu("To Workspace")
        self.workspace_submenu.setWindowFlags(Qt.FramelessWindowHint)
        wsets = sock.list_wsets()
        ws_grid_width = wsets[0]["workspace"]["grid_width"]
        ws_grid_height = wsets[0]["workspace"]["grid_height"]
        for i in range(ws_grid_height):
            for j in range(ws_grid_width):
                ws = int(i * ws_grid_width + j + 1)
                ws_submenu = self.workspace_submenu.addAction("Workspace " + str(ws))
                ws_submenu.triggered.connect(lambda *_, ws=ws: self.to_workspace(ws))
        self.workspace = self.addMenu(self.workspace_submenu)
        self.workspace.hovered.connect(lambda: self.submenu_hovered(self.workspace_submenu))
        self.workspace_submenu.hide()
        self.addSeparator()
        self.next_monitor_action = self.addAction("Move to Next Output")
        self.next_monitor_action.triggered.connect(self.option2_action)
        self.next_monitor_action.hovered.connect(lambda: self.item_hovered(self.next_monitor_action))
        self.prev_monitor_action = self.addAction("Move to Previous Output")
        self.prev_monitor_action.triggered.connect(self.option3_action)
        self.prev_monitor_action.hovered.connect(lambda: self.item_hovered(self.prev_monitor_action))
        self.addSeparator()
        #self.move_action = self.addAction("Move")
        #self.move_action.triggered.connect(self.option4_action)
        #self.move_action.hovered.connect(lambda: self.item_hovered(self.move_action))
        #self.resize_action = self.addAction("Resize")
        #self.resize_action.triggered.connect(self.option5_action)
        #self.resize_action.hovered.connect(lambda: self.item_hovered(self.resize_action))
        #self.addSeparator()
        self.maximize_action = self.addAction("Maximize")
        self.maximize_action.triggered.connect(self.option6_action)
        self.maximize_action.hovered.connect(lambda: self.item_hovered(self.maximize_action))
        self.restore_action = self.addAction("Restore")
        self.restore_action.triggered.connect(self.option7_action)
        self.restore_action.hovered.connect(lambda: self.item_hovered(self.restore_action))
        self.minimize_action = self.addAction("Minimize")
        self.minimize_action.triggered.connect(self.option8_action)
        self.minimize_action.hovered.connect(lambda: self.item_hovered(self.minimize_action))
        self.shade_action = self.addAction("Toggle Shade")
        self.shade_action.triggered.connect(self.do_shade)
        self.shade_action.hovered.connect(lambda: self.item_hovered(self.shade_action))
        self.addSeparator()
        self.layer_submenu = QMenu("Layer")
        self.layer_submenu.setWindowFlags(Qt.FramelessWindowHint)
        for l in ["Background", "Bottom", "Workspace", "Top", "Unmanaged", "Overlay", "Lock"]:
            action = self.layer_submenu.addAction(l)
            action.triggered.connect(lambda *_, layer=l: self.set_layer(layer))
        self.layer_menu = self.addMenu(self.layer_submenu)
        self.layer_menu.hovered.connect(lambda: self.submenu_hovered(self.layer_submenu))
        self.layer_submenu.triggered.connect(self.layer_submenu.hide)
        self.addSeparator()
        self.close_action = self.addAction("Close")
        self.close_action.triggered.connect(self.option11_action)
        self.close_action.hovered.connect(lambda: self.item_hovered(self.close_action))
        self.addSeparator()
        #self.exit_action = self.addAction("Exit Menu")
        #self.exit_action.triggered.connect(QApplication.instance().quit)
        #self.exit_action.hovered.connect(lambda: self.item_hovered(self.exit_action))
        self.workspace_submenu.setLayout(self.layout)
        QTimer.singleShot(1, self.show_popup_menu)

    def submenu_hovered(self, submenu):
        submenu.show()
        if submenu == self.workspace_submenu:
            self.layer_submenu.hide()
        if submenu == self.layer_submenu:
            self.workspace_submenu.hide()

    def item_hovered(self, menu_item):
        self.workspace_submenu.hide()
        self.layer_submenu.hide()

    def show_popup_menu(self):
        self.popup(self.mapToGlobal(QCursor.pos()))

    def set_layer(self, layer):
        print(f"To Layer: {layer}")
        message = get_msg_template("wf/menu/actions")
        message["data"]["action"] = "layer"
        message["data"]["layer"] = layer
        sock.send_json(message)

    def to_workspace(self, ws):
        print(f"To Workspace: {ws}")
        message = get_msg_template("wf/menu/actions")
        message["data"]["action"] = "to_workspace"
        message["data"]["workspace"] = ws
        sock.send_json(message)

    def option2_action(self):
        print("Move to Next Monitor")
        message = get_msg_template("wf/menu/actions")
        message["data"]["action"] = "next_output"
        sock.send_json(message)

    def option3_action(self):
        print("Move to Previous Monitor")
        message = get_msg_template("wf/menu/actions")
        message["data"]["action"] = "prev_output"
        sock.send_json(message)

    #def option4_action(self):
    #    print("Move")
    #    message = get_msg_template("wf/menu/actions")
    #    message["data"]["action"] = "move"
    #    sock.send_json(message)
	#
    #def option5_action(self):
    #    print("Resize")
    #    message = get_msg_template("wf/menu/actions")
    #    message["data"]["action"] = "resize"
    #    sock.send_json(message)

    def option6_action(self):
        print("Maximize")
        message = get_msg_template("wf/menu/actions")
        message["data"]["action"] = "maximize"
        sock.send_json(message)

    def option7_action(self):
        print("Restore")
        message = get_msg_template("wf/menu/actions")
        message["data"]["action"] = "restore"
        sock.send_json(message)

    def option8_action(self):
        print("Minimize")
        message = get_msg_template("wf/menu/actions")
        message["data"]["action"] = "minimize"
        sock.send_json(message)

    def do_shade(self):
        print("Shade")
        message = get_msg_template("wf/menu/actions")
        message["data"]["action"] = "shade_toggle"
        sock.send_json(message)

    def option10_action(self):
        print("Layer")
        message = get_msg_template("wf/menu/actions")
        message["data"]["action"] = "layer"
        sock.send_json(message)

    def option11_action(self):
        print("Close")
        message = get_msg_template("wf/menu/actions")
        message["data"]["action"] = "close"
        sock.send_json(message)

    def closeEvent(self, event: QEvent):
        print("Close called!")
        QApplication.instance().quit()

if __name__ == '__main__':
    QGuiApplication.setDesktopFileName("wf-menu")
    app = QApplication([])
    widget = MyWidget()
    widget.show()
    sys.exit(app.exec_())
