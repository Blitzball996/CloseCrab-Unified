package com.closecrab.plugin.actions;

import com.closecrab.plugin.CloseCrabClient;
import com.intellij.openapi.actionSystem.AnAction;
import com.intellij.openapi.actionSystem.AnActionEvent;
import com.intellij.openapi.actionSystem.CommonDataKeys;
import com.intellij.openapi.editor.Editor;
import com.intellij.openapi.ui.Messages;
import org.jetbrains.annotations.NotNull;

public class ExplainAction extends AnAction {
    @Override
    public void actionPerformed(@NotNull AnActionEvent e) {
        Editor editor = e.getData(CommonDataKeys.EDITOR);
        if (editor == null) return;
        String selected = editor.getSelectionModel().getSelectedText();
        if (selected == null || selected.isEmpty()) {
            Messages.showInfoMessage("No text selected", "CloseCrab");
            return;
        }
        try {
            CloseCrabClient client = new CloseCrabClient();
            String response = client.chat("Explain this code:\n```\n" + selected + "\n```");
            Messages.showInfoMessage(response, "CloseCrab - Explain");
        } catch (Exception ex) {
            Messages.showErrorDialog(ex.getMessage(), "CloseCrab Error");
        }
    }
}
