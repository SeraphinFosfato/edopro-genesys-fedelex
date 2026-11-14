package io.github.edo9300.edopro;

import android.app.AlertDialog;
import android.text.InputType;
import android.view.KeyEvent;
import android.widget.EditText;

public class TextEntry {

	private AlertDialog mTextInputDialog;
	private EditText mTextInputWidget;

	public void Show(android.content.Context context, String current) {
		mTextInputWidget = new EditText(context);
		mTextInputWidget.setMinWidth(300);
		mTextInputWidget.setInputType(InputType.TYPE_CLASS_TEXT);
		mTextInputWidget.setText(current);

		mTextInputWidget.setOnKeyListener((view, KeyCode, event) -> {
			if (KeyCode == KeyEvent.KEYCODE_ENTER) {
				pushResult(mTextInputWidget.getText().toString(), true);
				return true;
			}
			return false;
		});

		mTextInputDialog = new AlertDialog.Builder(context)
				.setView(mTextInputWidget)
				.setOnCancelListener(dialog -> pushResult(mTextInputWidget.getText().toString(), false))
				.create();

		mTextInputDialog.show();
	}

	private void pushResult(String text, boolean isenter) {
		EpNativeActivity.putMessageBoxResult(text, isenter);
		mTextInputDialog.dismiss();
	}
}
