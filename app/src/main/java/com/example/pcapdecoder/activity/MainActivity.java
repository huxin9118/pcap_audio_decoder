package com.example.pcapdecoder.activity;

import android.app.AlertDialog;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothHeadset;
import android.bluetooth.BluetoothProfile;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.media.AudioManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.app.Activity;
import android.os.Handler;
import android.os.Message;
import android.support.v7.widget.CardView;
import android.support.v7.widget.LinearLayoutManager;
import android.support.v7.widget.OrientationHelper;
import android.support.v7.widget.RecyclerView;
import android.util.Log;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.ViewGroup;
import android.view.inputmethod.InputMethodManager;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.ProgressBar;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.RelativeLayout;
import android.widget.TextView;
import android.widget.Toast;
import android.support.design.widget.FloatingActionButton;

import com.example.pcapdecoder.R;
import com.example.pcapdecoder.base.BaseRecyclerViewAdapter;
import com.example.pcapdecoder.base.OnItemClickListener;
import com.example.pcapdecoder.bean.AudioInfo;
import com.example.pcapdecoder.bean.PktInfo;

import java.io.File;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Set;
import java.util.Timer;
import java.util.TimerTask;
import java.util.TreeSet;

public class MainActivity extends Activity {

	private static final String PERMISSION_RECORD_AUDIO= "android.permission.RECORD_AUDIO";
	private static final String PERMISSION_WRITE_EXTERNAL_STORAGE= "android.permission.WRITE_EXTERNAL_STORAGE";
	private static final int PERMISSION_REQUESTCODE = 0;
	private static final int GET_CONTENT_REQUESTCODE = 0;
	private static final String OUTPUT_AUDIO_SUBSTRING_BEGIN = "AUDIO输出：";
	private static final String OUTPUT_PCM_SUBSTRING_BEGIN = "PCM输出：";
	private static final String TAG = MainActivity.class.getSimpleName();
	private Toast mToast;
	private boolean isShow;

	private FloatingActionButton btn_parse;
	private Timer floatingActionButtonTimer;
	private static ShowUIHandler showUIHandler;
	private ProgressBar progressBar;
	private TextView input_pcap;
	private EditText output_audio;
	private EditText output_pcm;
	private LinearLayout ly_output;
	private boolean isSelectPath = false;
	private static Handler progressRateHandler;
	private Thread parseThread;

	private RadioGroup parse_method;
	private int parse_method_type = 0;

	private AudioManager audioManager;
	private boolean isHeadSetOn = false;

	private RadioGroup parse_paramter;
	private RadioButton custom;
	private int parse_paramter_type = 0;

	private TextView label_sdp;
	private LinearLayout ly_none;
	private RecyclerView mainRecyclerView;
	private LinearLayoutManager mainLayoutManager;
	private MainActivityRecyclerAdapter mainRecyclerViewAdapter;
	private static ArrayList<AudioInfo> audioInfos;

	private PopupWindow analysePopupWindow;
	private TextView text_sum;
	private CheckBox check_normal;
	private CheckBox check_loss;
	private CheckBox check_disorder;
	public static Boolean isCheckNormal;
	public static Boolean isCheckLoss;
	public static Boolean isCheckDisorder;
	private RecyclerView popupRecyclerView;
	private LinearLayoutManager popupLayoutManager;
	private PopupWindowRecyclerAdapter popupRecyclerViewAdapter;
	private static ArrayList<PktInfo> pktInfos;
	private static ArrayList<PktInfo> filer_pktInfos;
	public static final int PKTINFO_STATUS_NORMAL = 0;
	public static final int PKTINFO_STATUS_LOSS = 1;
	public static final int PKTINFO_STATUS_DISORDER = 2;

	private AlertDialog.Builder customDialogbuilder;
	private AlertDialog.Builder isTryCustomDialogbuilder;

	private static long time_start;
	private static long time_end;

	private BroadcastReceiver headsetPlugReceiver = new BroadcastReceiver() {
		public void onReceive(Context context, Intent intent) {
			String action = intent.getAction();
			if (BluetoothHeadset.ACTION_CONNECTION_STATE_CHANGED.equals(action)) {
				BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
				if(BluetoothProfile.STATE_DISCONNECTED == adapter.getProfileConnectionState(BluetoothProfile.HEADSET)) {
					Log.d(TAG,"bluetooth disconnected");
					isHeadSetOn = false;
					audioManager.setSpeakerphoneOn(true);
				}
				else if(BluetoothProfile.STATE_CONNECTED == adapter.getProfileConnectionState(BluetoothProfile.HEADSET)) {
					Log.d(TAG, "bluetooth connected");
					audioManager.setSpeakerphoneOn(false);
				}
			}
			else if ("android.intent.action.HEADSET_PLUG".equals(action)) {
				if (intent.hasExtra("state")) {
					if (intent.getIntExtra("state", 0) == 0) {
						Log.d(TAG, "headset not connected");
						audioManager.setSpeakerphoneOn(true);
						isHeadSetOn = false;
					} else if (intent.getIntExtra("state", 0) == 1) {
						Log.d(TAG, "headset connected");
						audioManager.setSpeakerphoneOn(false);
						isHeadSetOn = true;
					}
				}
			}
		}
	};
	
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
		progressRateHandler = new progressRateHandler(this);
		btn_parse = (FloatingActionButton) this.findViewById(R.id.floatingActionButton);
		toggleBtnParse(false);

		progressBar = (ProgressBar) this.findViewById(R.id.progressBar);

		input_pcap = (TextView) this.findViewById(R.id.input_pcap);
		ly_output = (LinearLayout) this.findViewById(R.id.ly_output);
		ly_output.setAlpha(0.5f);
		output_audio= (EditText) this.findViewById(R.id.output_audio);
		output_audio.setEnabled(false);
		output_pcm= (EditText) this.findViewById(R.id.output_pcm);
		output_pcm.setEnabled(false);

		input_pcap.setOnClickListener(new OnClickListener() {
			public void onClick(View arg0) {
				if(Build.VERSION.SDK_INT > Build.VERSION_CODES.M) {
					if (checkSelfPermission(PERMISSION_WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED ||
							checkSelfPermission(PERMISSION_RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
						requestPermissions(new String[]{PERMISSION_WRITE_EXTERNAL_STORAGE, PERMISSION_RECORD_AUDIO}, PERMISSION_REQUESTCODE);
					} else {
						Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
						intent.setType("*/*");
						intent.addCategory(Intent.CATEGORY_OPENABLE);
						startActivityForResult(Intent.createChooser(intent, "请选择文件管理器"), GET_CONTENT_REQUESTCODE);
					}
				}
				else{
					Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
					intent.setType("*/*");
					intent.addCategory(Intent.CATEGORY_OPENABLE);
					startActivityForResult(Intent.createChooser(intent, "请选择文件管理器"), GET_CONTENT_REQUESTCODE);
				}
			}
		});

		btn_parse.setOnClickListener(new OnClickListener() {
			public void onClick(View arg0){
//				String folderurl=Environment.getExternalStorageDirectory().getPath();
				if(isSelectPath){
					if(audioInfos.size()>0 && mainRecyclerViewAdapter.positionSet.size()>0) {
						final String pcap_str = input_pcap.getText().toString();
						final String audio_str = output_audio.getText().toString().substring(OUTPUT_AUDIO_SUBSTRING_BEGIN.length());
						final String pcm_str = output_pcm.getText().toString().substring(OUTPUT_PCM_SUBSTRING_BEGIN.length());
						final AudioInfo audioInfo = audioInfos.get(mainRecyclerViewAdapter.positionSet.iterator().next());

						Log.i("pcap_str", pcap_str);
						Log.i("audio_str", audio_str);
						Log.i("pcm_str", pcm_str);
						Log.i("audioInfo", audioInfo.toString());
						parseThread = new Thread() {
							@Override
							public void run() {
								time_start = System.currentTimeMillis();
								updateAnalysePopupWindow(mainRecyclerViewAdapter.positionSet.iterator().next());
								if(parse_method_type == 0) {
									decode(pcap_str, audio_str, pcm_str, audioInfo);
								}
								else if(parse_method_type == 1) {
									play(pcap_str, audio_str, pcm_str, audioInfo);
								}
								else if(parse_method_type == 2) {
									getPayload(pcap_str, audio_str, pcm_str, audioInfo);
								}
							}
						};
						progressBar.setProgress(0);

						if(parse_method_type == 0) {
							progressBar.setMax((int)new File(pcap_str).length());
						}
						else if(parse_method_type == 1) {
							progressBar.setMax(audioInfo.getPkt_count() * audioInfo.getPtime()/20);
						}
						else if(parse_method_type == 2){
							progressBar.setMax((int)new File(pcap_str).length());
						}
						progressBar.setVisibility(View.VISIBLE);
						parseThread.start();
						btn_parse.setClickable(false);
						toggleBtnParse(false);
					}
					else{
						showToast("该PCAP文件没有发现可用的AMR/NOVC音频流",Toast.LENGTH_SHORT);
					}
				}
				else{
					showToast("请先选择PCAP路径",Toast.LENGTH_SHORT);
				}
			}
		});

		final SharedPreferences sp = getSharedPreferences("custom",Context.MODE_PRIVATE);
		parse_method_type = sp.getInt("parse_method_type",0);
		parse_paramter_type = sp.getInt("parse_paramter_type",0);

		parse_method = (RadioGroup) this.findViewById(R.id.parse_method);
		if(parse_method_type == 0) {
			parse_method.check(R.id.direct_decode);
		}
		else if(parse_method_type == 1) {
			parse_method.check(R.id.play_pcm);
		}
		else if(parse_method_type == 2){
			parse_method.check(R.id.get_payload);
		}
		parse_method.setOnCheckedChangeListener(new RadioGroup.OnCheckedChangeListener() {
			@Override
			public void onCheckedChanged(RadioGroup group, int checkedId) {
				if(checkedId == R.id.direct_decode){
					parse_method_type = 0;
					sp.edit().putInt("parse_method_type", 0);
					sp.edit().commit();

					output_pcm.setVisibility(View.VISIBLE);
				}
				else if(checkedId == R.id.play_pcm){
					parse_method_type = 1;
					sp.edit().putInt("parse_method_type", 1);
					sp.edit().commit();

					output_pcm.setVisibility(View.VISIBLE);
				}
				else if(checkedId == R.id.get_payload){
					parse_method_type = 2;
					sp.edit().putInt("parse_method_type", 2);
					sp.edit().commit();

					output_pcm.setVisibility(View.GONE);
				}
			}
		});

		parse_paramter = (RadioGroup) this.findViewById(R.id.payload_type);
		parse_paramter.check(parse_paramter_type == 0 ? R.id.parseSDP : R.id.custom);
		parse_paramter.setOnCheckedChangeListener(new RadioGroup.OnCheckedChangeListener() {
			@Override
			public void onCheckedChanged(RadioGroup group, int checkedId) {
				if(checkedId == R.id.parseSDP){
					parse_paramter_type = 0;
					sp.edit().putBoolean("isParseSDP", true);
					sp.edit().commit();

					String pcap_str = input_pcap.getText().toString();
					updateVideoInfo(pcap_str,false);
				}
				else{
					parse_paramter_type = 1;
					sp.edit().putBoolean("isParseSDP", false);
					sp.edit().commit();
				}
			}
		});

		custom = (RadioButton) this.findViewById(R.id.custom);
		custom.setOnClickListener(new OnClickListener() {
			@Override
			public void onClick(View v) {
				showCustomPayloadTypeDialog();
			}
		});

		IntentFilter bluetoothFilter = new IntentFilter(BluetoothHeadset.ACTION_CONNECTION_STATE_CHANGED);
		registerReceiver(headsetPlugReceiver, bluetoothFilter);

		IntentFilter headSetFilter = new IntentFilter();
		headSetFilter.addAction("android.intent.action.HEADSET_PLUG");
		registerReceiver(headsetPlugReceiver, headSetFilter);
		audioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
		isHeadSetOn = audioManager.isWiredHeadsetOn();
		if(isHeadSetOn) {
			audioManager.setSpeakerphoneOn(false);
		}

		label_sdp = (TextView) this.findViewById(R.id.label_sdp);
		ly_none = (LinearLayout) this.findViewById(R.id.ly_none);
		mainRecyclerView = (RecyclerView) this.findViewById(R.id.recyclerView);
		mainLayoutManager = new LinearLayoutManager(this);
		mainLayoutManager.setOrientation(OrientationHelper.VERTICAL);
		mainRecyclerView.setLayoutManager(mainLayoutManager);
		mainRecyclerViewAdapter = new MainActivityRecyclerAdapter(this);
		mainRecyclerViewAdapter.setCreateViewLayout(R.layout.item_recycler_main);

		mainRecyclerViewAdapter.setOnItemClickListener(new OnItemClickListener(){
			@Override
			public void onClick(View view,int pos,String viewName) {
				if ("itemView".equals(viewName)) {
					mainRecyclerViewAdapter.positionSet.clear();
					mainRecyclerViewAdapter.positionSet.add(pos);
					mainRecyclerViewAdapter.notifyDataSetChanged();
					String pcap_str = input_pcap.getText().toString();
					output_audio.setText(OUTPUT_AUDIO_SUBSTRING_BEGIN + pcap_str.substring(0, pcap_str.lastIndexOf(".")) + "."
							+ audioInfos.get(pos).getA_line().toLowerCase());
					output_audio.setSelection(output_audio.getText().length());
				}
				else if ("btn_analyse".equals(viewName)) {
					updateAnalysePopupWindow(pos);
					showAnalysePopupWindow();
				}
			}
		});
		mainRecyclerView.setOnScrollListener(new RecyclerView.OnScrollListener() {
			@Override
			public void onScrolled(RecyclerView recyclerView, int dx, int dy) {
				super.onScrolled(recyclerView, dx, dy);
				if(floatingActionButtonTimer == null) {
					floatingActionButtonTimer = new Timer();
					floatingActionButtonTimer.schedule(new TimerTask() {
						@Override
						public void run() {
							showUIHandler.sendEmptyMessage(0);
						}
					}, 1000);
					btn_parse.setVisibility(View.GONE);
				}
				else{
					floatingActionButtonTimer.cancel();
					floatingActionButtonTimer = new Timer();
					floatingActionButtonTimer.schedule(new TimerTask() {
						@Override
						public void run() {
							showUIHandler.sendEmptyMessage(0);
						}
					}, 1000);
					btn_parse.setVisibility(View.GONE);
				}
			}
		});
		showUIHandler = new ShowUIHandler(this);
		label_sdp.setVisibility(View.GONE);
		ly_none.setVisibility(View.GONE);
		mainRecyclerView.setVisibility(View.GONE);

		initAnalysePopupWindow();
    }

	void updateFilterPktInfos(){
		if(isCheckNormal && isCheckLoss && isCheckDisorder) {
			filer_pktInfos = pktInfos;
		}
		else{
			filer_pktInfos = new ArrayList<PktInfo>();
			for(int i = 0; i<pktInfos.size(); i++ ){
				if(pktInfos.get(i).getStatus() == PKTINFO_STATUS_NORMAL && isCheckNormal){
					filer_pktInfos.add(pktInfos.get(i));
				}
				else if(pktInfos.get(i).getStatus() == PKTINFO_STATUS_LOSS && isCheckLoss){
					filer_pktInfos.add(pktInfos.get(i));
				}
				else if(pktInfos.get(i).getStatus() == PKTINFO_STATUS_DISORDER && isCheckDisorder){
					filer_pktInfos.add(pktInfos.get(i));
				}
			}
		}
		text_sum.setText("("+filer_pktInfos.size()+"):");
		popupRecyclerViewAdapter.addDatas("pkts", filer_pktInfos);
	}

	private void initAnalysePopupWindow() {
		View contentView = LayoutInflater.from(this).inflate(R.layout.popupwindow_analyse, null);
		analysePopupWindow = new PopupWindow(contentView, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT, true);
		text_sum = (TextView) contentView.findViewById(R.id.text_sum);
		check_normal = (CheckBox)contentView.findViewById(R.id.check_normal);
		check_loss = (CheckBox)contentView.findViewById(R.id.check_loss);
		check_disorder = (CheckBox)contentView.findViewById(R.id.check_disorder);
		isCheckNormal = true;
		isCheckLoss = true;
		isCheckDisorder = true;
		check_normal.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
			@Override
			public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
				isCheckNormal = isChecked;
				updateFilterPktInfos();
			}
		});
		check_loss.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
			@Override
			public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
				isCheckLoss = isChecked;
				updateFilterPktInfos();
			}
		});
		check_disorder.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
			@Override
			public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
				isCheckDisorder = isChecked;
				updateFilterPktInfos();
			}
		});
		popupRecyclerView =  (RecyclerView)contentView.findViewById(R.id.recyclerView);
		popupLayoutManager = new LinearLayoutManager(this);
		popupLayoutManager.setOrientation(OrientationHelper.VERTICAL);
		popupRecyclerView.setLayoutManager(popupLayoutManager);
		popupRecyclerViewAdapter = new PopupWindowRecyclerAdapter(this);
		popupRecyclerViewAdapter.setCreateViewLayout(R.layout.item_recycler_popup);
		pktInfos = new ArrayList<PktInfo>();
		popupRecyclerViewAdapter.addDatas("pkts", pktInfos);
		popupRecyclerView.setAdapter(popupRecyclerViewAdapter);
	}

	private void updateAnalysePopupWindow(int pos){
		pktInfos.clear();
		String pcap_str = input_pcap.getText().toString();
		int stutas = parsePktInfo(pcap_str, pktInfos, audioInfos.get(pos));
		if (stutas != 0) {
			switch (stutas) {
				case -1:
					showToast("音频流分析失败，该音频流可能已经损坏！", Toast.LENGTH_SHORT);
					break;
			}
		}
		else {
			Collections.sort(pktInfos);
			for(int i = 1; i<pktInfos.size(); i++) {
				if(pktInfos.get(i).getTimestamp() < pktInfos.get(i-1).getTimestamp()){
					pktInfos.get(i).setStatus(PKTINFO_STATUS_DISORDER);
					pktInfos.get(i-1).setStatus(PKTINFO_STATUS_DISORDER);
				}
			}
			for(int i = 1; i<pktInfos.size(); i++){
				for(int j = PktInfo.shortiToUshort(pktInfos.get(i).getSeq()) - 1; j > PktInfo.shortiToUshort(pktInfos.get(i-1).getSeq()); j--){
					PktInfo lostPkt = new PktInfo();
					lostPkt.setSeq((short)j);
					lostPkt.setStatus(PKTINFO_STATUS_LOSS);
					pktInfos.add(i, lostPkt);
				}
			}
		}
		updateFilterPktInfos();
	}

	private void showAnalysePopupWindow(){
		popupRecyclerViewAdapter.notifyDataSetChanged();
		popupRecyclerView.smoothScrollToPosition(0);
		View rootview = LayoutInflater.from(this).inflate(R.layout.activity_main, null);
		analysePopupWindow.showAtLocation(rootview, Gravity.CENTER, 0, 0);
	}

	private void showCustomPayloadTypeDialog(){
		customDialogbuilder = new AlertDialog.Builder(this);
		customDialogbuilder.setTitle("自定义解码参数");
		LayoutInflater layoutInflater = getLayoutInflater();
		View dialogView = layoutInflater.inflate(R.layout.dialog_custom,null);
		final EditText edit_amr_type_num = (EditText) dialogView.findViewById(R.id.amr_type_num);
		final EditText edit_amr_ptime = (EditText) dialogView.findViewById(R.id.amr_ptime);
		final EditText edit_nvoc_type_num = (EditText) dialogView.findViewById(R.id.nvoc_type_num);
		final EditText edit_nvoc_ptime = (EditText) dialogView.findViewById(R.id.nvoc_ptime);
		final RadioGroup radio_nvoc_max_kbps = (RadioGroup) dialogView.findViewById(R.id.nvoc_max_kbps);
		final RadioGroup radio_nvoc_fec = (RadioGroup) dialogView.findViewById(R.id.nvoc_fec);

		final SharedPreferences sp = getSharedPreferences("custom",Context.MODE_PRIVATE);
		edit_amr_type_num.setText(sp.getInt("amr_type_num",126)+"");
		edit_amr_type_num.setSelection(edit_amr_type_num.getText().length());
		edit_amr_ptime.setText(sp.getInt("amr_ptime",20)+"");
		edit_nvoc_type_num.setText(sp.getInt("nvoc_type_num",93)+"");
		edit_nvoc_ptime.setText(sp.getInt("nvoc_ptime",60)+"");
		radio_nvoc_max_kbps.check( sp.getFloat("nvoc_max_kbps",2.4f) == 2.4f ? R.id.nvoc_max_kbps_2_4 : R.id.nvoc_max_kbps_2_2 );
		radio_nvoc_fec.check( sp.getBoolean("nvoc_fec",true) ? R.id.nvoc_fec_on : R.id.nvoc_fec_off );

		customDialogbuilder.setView(dialogView);
		customDialogbuilder.setPositiveButton("确定", null);
		customDialogbuilder.setNegativeButton("取消", null);
		customDialogbuilder.setNeutralButton("恢复默认值",null);
		customDialogbuilder.setCancelable(false);
		final AlertDialog dialog = customDialogbuilder.show();
		dialog.getButton(DialogInterface.BUTTON_NEGATIVE).setTextColor(getResources().getColor(R.color.customGreen));
		dialog.getButton(DialogInterface.BUTTON_POSITIVE).setTextColor(getResources().getColor(R.color.customBlue));
		dialog.getButton(DialogInterface.BUTTON_NEUTRAL).setTextColor(getResources().getColor(R.color.textSecondaryColor));
		dialog.getButton(DialogInterface.BUTTON_NEGATIVE).setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				InputMethodManager manager= (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
				manager.hideSoftInputFromWindow( dialog.getCurrentFocus().getWindowToken(), InputMethodManager.HIDE_NOT_ALWAYS);
				dialog.dismiss();

				String pcap_str = input_pcap.getText().toString();
				updateVideoInfo(pcap_str,false);
			}
		});
		dialog.getButton(DialogInterface.BUTTON_NEUTRAL).setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				SharedPreferences.Editor editor = sp.edit();
				editor.putInt("amr_type_num",126);
				editor.putInt("amr_ptime",20);
				editor.putInt("nvoc_type_num",93);
				editor.putInt("nvoc_ptime",60);
				editor.putFloat("nvoc_max_kbps",2.4f);
				editor.putBoolean("nvoc_fec",true);
				editor.commit();

				edit_amr_type_num.setText(sp.getInt("amr_type_num",126)+"");
				edit_amr_type_num.setSelection(edit_amr_type_num.getText().length());
				edit_amr_ptime.setText(sp.getInt("amr_ptime",20)+"");
				edit_nvoc_type_num.setText(sp.getInt("nvoc_type_num",93)+"");
				edit_nvoc_ptime.setText(sp.getInt("nvoc_ptime",60)+"");
				radio_nvoc_max_kbps.check( sp.getFloat("nvoc_max_kbps",2.4f) == 2.4f ? R.id.nvoc_max_kbps_2_4 : R.id.nvoc_max_kbps_2_2 );
				radio_nvoc_fec.check( sp.getBoolean("nvoc_fec",true) ? R.id.nvoc_fec_on : R.id.nvoc_fec_off );
			}
		});
		dialog.getButton(DialogInterface.BUTTON_POSITIVE).setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				int amr_type_num = 126;
				int nvoc_type_num = 93;
				int amr_ptime = 20;
				int nvoc_ptime = 60;
				try {
					amr_type_num = Integer.parseInt(edit_amr_type_num.getText().toString());
					if(amr_type_num < 0 && amr_type_num > 127) {
						showToast("PayLoad Type参数范围为0~127，请输入正确的参数",Toast.LENGTH_SHORT);
					}
					nvoc_type_num = Integer.parseInt(edit_nvoc_type_num.getText().toString());
					if(nvoc_type_num < 0 && nvoc_type_num > 127) {
						showToast("PayLoad Type参数范围为 0~127 ，请输入正确的参数",Toast.LENGTH_SHORT);
					}
				}
				catch (NumberFormatException e){
					showToast("PayLoad Type参数范围为 0~127 ，请输入正确的参数",Toast.LENGTH_SHORT);
				}

				try {
					amr_ptime = Integer.parseInt(edit_amr_ptime.getText().toString());
					if(amr_ptime % 20 != 0) {
						showToast("Ptime参数必须为 20 的整数倍，请输入正确的参数",Toast.LENGTH_SHORT);
					}
					nvoc_ptime = Integer.parseInt(edit_nvoc_ptime.getText().toString());
					if(nvoc_ptime % 20 != 0) {
						showToast("Ptime参数必须为 20 的整数倍，请输入正确的参数",Toast.LENGTH_SHORT);
					}
				}
				catch (NumberFormatException e){
					showToast("Ptime参数必须为 20 的整数倍，请输入正确的参数",Toast.LENGTH_SHORT);
				}

				SharedPreferences.Editor editor = sp.edit();
				editor.putInt("amr_type_num",amr_type_num);
				editor.putInt("amr_ptime",amr_ptime);
				editor.putInt("nvoc_type_num",nvoc_type_num);
				editor.putInt("nvoc_ptime",nvoc_ptime);
				editor.putFloat("nvoc_max_kbps",radio_nvoc_max_kbps.getCheckedRadioButtonId() == R.id.nvoc_max_kbps_2_4 ? 2.4f : 2.2f);
				editor.putBoolean("nvoc_fec",radio_nvoc_fec.getCheckedRadioButtonId() == R.id.nvoc_fec_on);
				editor.commit();

				InputMethodManager manager= (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
				manager.hideSoftInputFromWindow( dialog.getCurrentFocus().getWindowToken(), InputMethodManager.HIDE_NOT_ALWAYS);
				dialog.dismiss();

				String pcap_str = input_pcap.getText().toString();
				updateVideoInfo(pcap_str,false);
			}
		});
	}

	@Override
	public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
		switch (requestCode) {
			case PERMISSION_REQUESTCODE:
				if (PERMISSION_WRITE_EXTERNAL_STORAGE.equals(permissions[0]) &&
						PERMISSION_RECORD_AUDIO.equals(permissions[1]) &&
						grantResults[0] == PackageManager.PERMISSION_GRANTED &&
						grantResults[1] == PackageManager.PERMISSION_GRANTED) {
					Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
					intent.setType("*/*");
					intent.addCategory(Intent.CATEGORY_OPENABLE);
					startActivityForResult(Intent.createChooser(intent,"请选择文件管理器"),0);
				}
				else{
					showToast("请允许应用程序所需权限，否则无法正常工作！",Toast.LENGTH_SHORT);
				}
		}
	}

	private void showIsTryCustomParseDialog() {
		isTryCustomDialogbuilder = new AlertDialog.Builder(this);
		isTryCustomDialogbuilder.setTitle("未获取到可解析的音频流");
		if(parse_paramter_type == 0) {
			isTryCustomDialogbuilder.setMessage("PCAP包SDP信息缺失或出错，是否自定义解析参数？");
		}
		else if(parse_paramter_type == 1) {
			isTryCustomDialogbuilder.setMessage("目前自定义解析参数可能有误，是否更新自定义解析参数？");
		}

		isTryCustomDialogbuilder.setPositiveButton("确定", new DialogInterface.OnClickListener() {
			@Override
			public void onClick(DialogInterface dialog, int which) {
				parse_paramter.check(R.id.custom);
				custom.performClick();
				dialog.dismiss();
			}
		});
		isTryCustomDialogbuilder.setNegativeButton("取消", new DialogInterface.OnClickListener() {
			@Override
			public void onClick(DialogInterface dialog, int which) {
				dialog.dismiss();
			}
		});
	}


	void updateVideoInfo(String input_url, Boolean isNewPath){
		if(isSelectPath) {
			audioInfos = new ArrayList<AudioInfo>();
			if (parse_paramter_type == 1) {
				final SharedPreferences sp = getSharedPreferences("custom", Context.MODE_PRIVATE);
				AudioInfo amr_paramter = new AudioInfo();
				amr_paramter.setType_num(sp.getInt("amr_type_num", 126));
				amr_paramter.setPtime(sp.getInt("amr_ptime", 20));
				audioInfos.add(amr_paramter);

				AudioInfo nvoc_paramter = new AudioInfo();
				nvoc_paramter.setType_num(sp.getInt("nvoc_type_num", 93));
				nvoc_paramter.setPtime(sp.getInt("nvoc_ptime", 60));
				nvoc_paramter.setMax_kbps(sp.getFloat("nvoc_max_kbps",2.4f) == 2.4f ? 0 : 1);
				nvoc_paramter.setFec(sp.getBoolean("nvoc_fec",true) ? 1 : 0);
				audioInfos.add(nvoc_paramter);
			}
			int stutas = parseAudioInfo(input_url, parse_paramter_type, audioInfos);
			if (stutas != 0) {
				switch (stutas) {
					case -1:
						showToast("无法打开音频文件，请检查是否拥有读写权限！", Toast.LENGTH_SHORT);
						break;
				}
			}
			else {
				label_sdp.setVisibility(View.VISIBLE);
				if (audioInfos.size() > 0) {
					mainRecyclerViewAdapter.addDatas("infos", audioInfos);
					mainRecyclerViewAdapter.positionSet.clear();
					mainRecyclerViewAdapter.positionSet.add(0);
					mainRecyclerView.setVisibility(View.VISIBLE);
					mainRecyclerView.setAdapter(mainRecyclerViewAdapter);
					ly_none.setVisibility(View.GONE);
					toggleBtnParse(true);

					Log.i(TAG, audioInfos.get(0).toString());

					ly_output.setAlpha(1);
					input_pcap.setText(input_url);
					output_audio.setEnabled(true);
					output_audio.setText(OUTPUT_AUDIO_SUBSTRING_BEGIN + input_url.substring(0, input_url.lastIndexOf(".")) + "."
							+ audioInfos.get(0).getA_line().toLowerCase());
					output_audio.setSelection(output_audio.getText().length());
					output_pcm.setEnabled(true);
					output_pcm.setText(OUTPUT_PCM_SUBSTRING_BEGIN + input_url.substring(0, input_url.lastIndexOf(".")) + ".pcm");
					output_pcm.setSelection(output_pcm.getText().length());
				} else {
					ly_none.setVisibility(View.VISIBLE);
					mainRecyclerView.setVisibility(View.GONE);
					toggleBtnParse(false);

					ly_output.setAlpha(0.5f);
					input_pcap.setText(input_url);
					output_audio.setEnabled(false);
					output_audio.setText("");
					output_pcm.setEnabled(false);
					output_pcm.setText("");

					if(isNewPath){
						showIsTryCustomParseDialog();
					}
				}
			}
		}
	}

	protected void onActivityResult(int requestCode, int resultCode, Intent data) {
		if(resultCode == Activity.RESULT_OK){
			switch (requestCode){
				case GET_CONTENT_REQUESTCODE:
					Uri uri = data.getData();
					String input_url = uri.getPath();
					if("/root".equals(input_url.substring(0,5))){
						input_url = input_url.substring(5);
					}
					if(input_url.toLowerCase().endsWith(".pcap")) {
						isSelectPath = true;
						updateVideoInfo(input_url, true);
					}
					else{
						showToast(input_url+"打开失败，该文件不是PCAP文件！", Toast.LENGTH_SHORT);
					}
				break;
			}
		}
	}
	static class ShowUIHandler extends Handler {
		private WeakReference<MainActivity> weakReference;
		public ShowUIHandler(MainActivity sdlActivity){
			weakReference = new WeakReference<MainActivity>(sdlActivity);
		}
		@Override
		public void handleMessage(Message msg) {
			final MainActivity activity= weakReference.get();
			if(activity != null) {
				switch (msg.what) {
					case 0:
						activity.btn_parse.setVisibility(View.VISIBLE);
						break;
				}
			}
		}
	}
	static class progressRateHandler extends Handler {
		WeakReference<MainActivity> mActivityReference;
		progressRateHandler(MainActivity activity) {
			mActivityReference= new WeakReference(activity);
		}
		@Override
		public void handleMessage(Message msg) {
			final MainActivity activity = mActivityReference.get();
			if (activity != null) {
				if(msg.what == -1) {
					activity.progressBar.setProgress(activity.progressBar.getMax());
					activity.showToast("PCAP解析完成", Toast.LENGTH_SHORT);
					activity.btn_parse.postDelayed(new Runnable() {
						@Override
						public void run() {
							activity.btn_parse.setClickable(true);
							activity.toggleBtnParse(true);
							activity.progressBar.setVisibility(View.INVISIBLE);
						}
					},200);
				}
				else if(msg.what == -2) {
					activity.progressBar.setProgress(0);
					activity.showToast("PCAP解析取消", Toast.LENGTH_SHORT);
					activity.btn_parse.setClickable(true);
					activity.toggleBtnParse(true);
					activity.progressBar.setVisibility(View.INVISIBLE);
				}
				else {
					if(activity.isShow) {
						if(activity.parse_method_type == 0) {
							activity.showToast("已解析 " + msg.what + " 字节", Toast.LENGTH_SHORT);
						}
						else if(activity.parse_method_type == 1) {
							activity.showToast("已播放 " + msg.what + " 帧", Toast.LENGTH_SHORT);
						}
						else if(activity.parse_method_type == 2) {
							activity.showToast("已解析 " + msg.what + " 字节", Toast.LENGTH_SHORT);
						}
					}
					activity.progressBar.setProgress(msg.what);
				}
			}
		}
	}

	//JNI
	public static void setProgressRate(int progress){
		progressRateHandler.sendEmptyMessage(progress);
//		Log.i("++++++", progress+"");
	}

	public static void setProgressRateFull(){
		time_end = System.currentTimeMillis();
		Log.i("time_duration", ""+((double)(time_end - time_start))/1000);
		progressRateHandler.sendEmptyMessage(-1);
//		Log.i("+++---", -1+"");
	}
	public static void setProgressRateEmpty(){
		progressRateHandler.sendEmptyMessage(-2);
//		Log.i("+++---", -2+"");
	}

	public native int parseAudioInfo(String pcap_str, int parse_paramter_type, ArrayList audioInfos);
	public native int parsePktInfo(String pcap_str, ArrayList pktInfos, AudioInfo audioInfo);
	public native int play(String pcap_str, String audio_str, String pcm_str, AudioInfo audioInfo);
	public native void playCancel();
    public native int decode(String pcap_str, String audio_str, String pcm_str, AudioInfo audioInfo);
	public native void decodeCancel();
	public native int getPayload(String pcap_str, String audio_str, String pcm_str, AudioInfo audioInfo);
	public native void getPayloadCancel();
	//public native int decode2(String pcap_str, String audio_str, String pcm_str, AudioInfo audioInfo);

	static {
		System.loadLibrary("native_audio");
		//System.loadLibrary("native_nvoc");
	}

	void toggleBtnParse(boolean isClickable){
		if(isClickable) {
			btn_parse.setImageResource(R.drawable.ic_export_send_while);
			btn_parse.setBackgroundTintList(ColorStateList.valueOf(getResources().getColor(R.color.customBlue)));
			btn_parse.setRippleColor(getResources().getColor(R.color.colorPrimary));
		}
		else {
			btn_parse.setImageResource(R.drawable.ic_export_send_while);
			btn_parse.setBackgroundTintList(ColorStateList.valueOf(getResources().getColor(R.color.darker_gray)));
			btn_parse.setRippleColor(getResources().getColor(R.color.lighter_gray));
		}
	}
	@Override
	protected void onResume() {
		super.onResume();
		isShow = true;
	}

	@Override
	protected void onPause() {
		super.onPause();
		isShow = false;
		cancelToast();
	}

	@Override
	protected void onDestroy() {
		super.onDestroy();
		unregisterReceiver(headsetPlugReceiver);
		cancelToast();
		if(floatingActionButtonTimer != null){
			floatingActionButtonTimer.cancel();
			floatingActionButtonTimer = null;
		}
	}

	/**
	 * 显示Toast，解决重复弹出问题
	 */
	public void showToast(String text , int time) {
		if(mToast == null) {
			mToast = Toast.makeText(this, text, time);
		} else {
			mToast.setText(text);
			mToast.setDuration(Toast.LENGTH_SHORT);
		}
		mToast.show();
	}

	/**
	 * 隐藏Toast
	 */
	public void cancelToast() {
		if (mToast != null) {
			mToast.cancel();
			mToast = null;
		}
	}


	public void onBackPressed() {
		if(parseThread != null && parseThread.isAlive()){
			if(parse_method_type == 0) {
				decodeCancel();
			}
			else if(parse_method_type == 0) {
				playCancel();
			}
			else if(parse_method_type == 0) {
				getPayloadCancel();
			}
			cancelToast();
		}
		else{
			cancelToast();
			super.onBackPressed();
		}
	}
}

class MainActivityRecyclerAdapter extends BaseRecyclerViewAdapter {
	private Context mContext;
	public Set<Integer> positionSet;
	public boolean isSelectAll = false;

	public MainActivityRecyclerAdapter(Context context) {
		super(context);
		mContext = context;
		positionSet = new TreeSet<Integer>();
	}

	@Override
	public int getItemCount() {
		if (mHeaderView == null && mFooterView == null) return mDatas.get("infos").size();
		if (mHeaderView != null && mFooterView != null) return mDatas.get("infos").size() + 2;
		return mDatas.get("infos").size() + 1;
	}

	@Override
	public RecyclerView.ViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
		if (viewType == TYPE_HEADER) return new Holder(mHeaderView, viewType);
		if (viewType == TYPE_FOOTER) return new Holder(mFooterView, viewType);
		View layout = mInflater.inflate(mCreateViewLayout, parent, false);
		return new Holder(layout, viewType);
	}

	@Override
	public void onBindViewHolder(RecyclerView.ViewHolder viewHolder, int position) {
		Holder holder = (Holder) viewHolder;
		final int pos = getRealPosition(holder);
		if (getItemViewType(position) == TYPE_HEADER || getItemViewType(position) == TYPE_FOOTER)
			return;
		if (getItemViewType(position) == TYPE_NORMAL) {
			AudioInfo info = (AudioInfo)mDatas.get("infos").get(pos);
			String ip_src = AudioInfo.byteArrayToIP(AudioInfo.intToByteArray(Integer.valueOf(info.getIp_src())));
			String ip_dst = AudioInfo.byteArrayToIP(AudioInfo.intToByteArray(Integer.valueOf(info.getIp_dst())));
			long ssrc = AudioInfo.intToUint(info.getSsrc());
			int type_num = info.getType_num();
			int pkt_count = info.getPkt_count();
			String a_line = info.getA_line();
			//holder.img.setCardBackgroundColor(ConstantUtils.getColorByID(pos%ConstantUtils.SUM_ID));
			holder.type.setText(a_line);
			holder.ip_src.setText(ip_src);
			holder.ip_dst.setText(ip_dst);
			holder.ssrc.setText(ssrc+"");
			holder.type_num.setText(type_num+"");
			holder.pkt_count.setText(pkt_count+"");
			holder.itemView.setOnClickListener(new OnClickListener() {
				@Override
				public void onClick(View v) {
					mOnItemClickListener.onClick(v, pos, "itemView");
				}
			});
			holder.btn_analyse.setOnClickListener(new OnClickListener() {
				@Override
				public void onClick(View v) {
					mOnItemClickListener.onClick(v, pos, "btn_analyse");
				}
			});
			if(pos == 0){
				holder.line.setVisibility(View.INVISIBLE);
			}
			else{
				holder.line.setVisibility(View.VISIBLE);
			}
			if (positionSet.contains(pos)) {
				holder.itemView.setBackgroundColor(mContext.getResources().getColor(R.color.lighter_gray));
			} else {
				holder.itemView.setBackgroundColor(mContext.getResources().getColor(R.color.transparent));
			}
		}
	}

	static class Holder extends RecyclerView.ViewHolder {
		public TextView ip_src;
		public TextView ip_dst;
		public TextView ssrc;
		public TextView pkt_count;
		public TextView type_num;
		public CardView img;
		public TextView type;
		public ImageView btn_analyse;
		public RelativeLayout back;
		public View line;

		public Holder(View itemView, int viewType) {
			super(itemView);
			if (viewType == TYPE_HEADER || viewType == TYPE_FOOTER) return;
			if (viewType == TYPE_NORMAL) {
				back = (RelativeLayout) itemView.findViewById(R.id.back);
				ip_src = (TextView) itemView.findViewById(R.id.ip_src);
				ip_dst = (TextView) itemView.findViewById(R.id.ip_dst);
				ssrc = (TextView) itemView.findViewById(R.id.ssrc);
				pkt_count = (TextView) itemView.findViewById(R.id.pkt_count);
				type_num = (TextView) itemView.findViewById(R.id.type_num);
				img = (CardView) itemView.findViewById(R.id.img);
				btn_analyse = (ImageView) itemView.findViewById(R.id.btn_analyse);
				type = (TextView) itemView.findViewById(R.id.type);
				line = itemView.findViewById(R.id.line);
			}
		}
	}
}

class PopupWindowRecyclerAdapter extends BaseRecyclerViewAdapter {
	private Context mContext;

	public PopupWindowRecyclerAdapter(Context context) {
		super(context);
		mContext = context;
	}

	@Override
	public int getItemCount() {
		if (mHeaderView == null && mFooterView == null) return mDatas.get("pkts").size();
		if (mHeaderView != null && mFooterView != null) return mDatas.get("pkts").size() + 2;
		return mDatas.get("pkts").size() + 1;
	}

	@Override
	public RecyclerView.ViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
		if (viewType == TYPE_HEADER) return new Holder(mHeaderView, viewType);
		if (viewType == TYPE_FOOTER) return new Holder(mFooterView, viewType);
		View layout = mInflater.inflate(mCreateViewLayout, parent, false);
		return new Holder(layout, viewType);
	}

	@Override
	public void onBindViewHolder(RecyclerView.ViewHolder viewHolder, int position) {
		Holder holder = (Holder) viewHolder;
		final int pos = getRealPosition(holder);
		if (getItemViewType(position) == TYPE_HEADER || getItemViewType(position) == TYPE_FOOTER)
			return;
		if (getItemViewType(position) == TYPE_NORMAL) {
			PktInfo info = (PktInfo)mDatas.get("pkts").get(pos);

			if(info.getStatus() == MainActivity.PKTINFO_STATUS_NORMAL){
				holder.status.setText("正常");
				holder.frame.setTextColor(mContext.getResources().getColor(R.color.textSecondaryColor));
				holder.seq.setTextColor(mContext.getResources().getColor(R.color.textSecondaryColor));
				holder.timestamp.setTextColor(mContext.getResources().getColor(R.color.textSecondaryColor));
				holder.status.setTextColor(mContext.getResources().getColor(R.color.textSecondaryColor));

				holder.frame.setText(info.getFrame()+"");
				holder.seq.setText(PktInfo.shortiToUshort(info.getSeq())+"");
				holder.timestamp.setText(PktInfo.intToUint(info.getTimestamp())+"");
			}
			else if(info.getStatus() == MainActivity.PKTINFO_STATUS_LOSS){
				holder.status.setText("丢包");
				holder.frame.setTextColor(mContext.getResources().getColor(R.color.orangered));
				holder.seq.setTextColor(mContext.getResources().getColor(R.color.orangered));
				holder.timestamp.setTextColor(mContext.getResources().getColor(R.color.orangered));
				holder.status.setTextColor(mContext.getResources().getColor(R.color.orangered));

				holder.frame.setText("——");
				holder.seq.setText(PktInfo.shortiToUshort(info.getSeq())+"");
				holder.timestamp.setText("——");
			}
			else if(info.getStatus() == MainActivity.PKTINFO_STATUS_DISORDER){
				holder.status.setText("乱序");
				holder.frame.setTextColor(mContext.getResources().getColor(R.color.mediumblue));
				holder.seq.setTextColor(mContext.getResources().getColor(R.color.mediumblue));
				holder.timestamp.setTextColor(mContext.getResources().getColor(R.color.mediumblue));
				holder.status.setTextColor(mContext.getResources().getColor(R.color.mediumblue));

				holder.frame.setText(info.getFrame()+"");
				holder.seq.setText(PktInfo.shortiToUshort(info.getSeq())+"");
				holder.timestamp.setText(PktInfo.intToUint(info.getTimestamp())+"");
			}
		}
	}

	static class Holder extends RecyclerView.ViewHolder {
		public TextView frame;
		public TextView seq;
		public TextView timestamp;
		public TextView status;

		public Holder(View itemView, int viewType) {
			super(itemView);
			if (viewType == TYPE_HEADER || viewType == TYPE_FOOTER) return;
			if (viewType == TYPE_NORMAL) {
				frame = (TextView) itemView.findViewById(R.id.frame);
				seq = (TextView) itemView.findViewById(R.id.seq);
				timestamp = (TextView) itemView.findViewById(R.id.timestamp);
				status = (TextView) itemView.findViewById(R.id.status);
			}
		}
	}
}