// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

package com.deepin.assistant.ui.activity

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.content.*
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.text.SpannableString
import android.text.Spanned
import android.text.method.LinkMovementMethod
import android.text.style.ClickableSpan
import android.util.Base64
import android.util.Log
import android.util.Pair
import android.view.View
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.core.content.ContextCompat
import androidx.lifecycle.Observer
import androidx.lifecycle.ViewModelProvider
import androidx.preference.PreferenceManager
import androidx.viewpager.widget.ViewPager
import com.gyf.immersionbar.ImmersionBar
import com.hjq.base.FragmentPagerAdapter
import com.deepin.assistant.R
import com.deepin.assistant.app.AppActivity
import com.deepin.assistant.app.AppFragment
import com.deepin.assistant.manager.*
import com.deepin.assistant.model.SharedViewModel
import com.deepin.assistant.other.DoubleClickHelper
import com.deepin.assistant.ui.adapter.NavigationAdapter
import com.deepin.assistant.ui.fragment.HomeFragment
import com.deepin.assistant.ui.fragment.MessageFragment
import com.deepin.assistant.ui.fragment.MineFragment
import com.deepin.assistant.ui.fragment.FirstFragment
import com.deepin.cooperation.CooperationListener
import com.deepin.cooperation.JniCooperation
import com.hjq.permissions.Permission
import net.christianbeier.droidvnc_ng.Constants
import net.christianbeier.droidvnc_ng.Defaults
import net.christianbeier.droidvnc_ng.InputRequestActivity
import net.christianbeier.droidvnc_ng.InputService
import net.christianbeier.droidvnc_ng.MainService
import net.christianbeier.droidvnc_ng.MediaProjectionService
import net.christianbeier.droidvnc_ng.Utils
import org.json.JSONObject

class HomeActivity : AppActivity(), NavigationAdapter.OnNavigationListener {

    companion object {
        private const val TAG = "HomeActivity"
        private const val INTENT_KEY_IN_FRAGMENT_INDEX: String = "fragmentIndex"
        private const val INTENT_KEY_IN_FRAGMENT_CLASS: String = "fragmentClass"

        @JvmOverloads
        fun start(context: Context, fragmentClass: Class<out AppFragment<*>?>? = FirstFragment::class.java) {
            val intent = Intent(context, HomeActivity::class.java)
            intent.putExtra(INTENT_KEY_IN_FRAGMENT_CLASS, fragmentClass)
            if (context !is Activity) {
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            context.startActivity(intent)
        }
    }

    private lateinit var viewModel: SharedViewModel
    private var mCooperation: JniCooperation? = null

    private var mIsMainServiceRunning = false
    private var mMainServiceBroadcastReceiver: BroadcastReceiver? = null
    private var mOutgoingConnectionWaitDialog: AlertDialog? = null
    private var mLastMainServiceRequestId: String? = null
    private var mLastReverseHost: String? = null
    private var mLastReversePort = 0
    private var mDefaults: Defaults? = null

    private val viewPager: ViewPager? by lazy { findViewById(R.id.vp_home_pager) }
    private var navigationAdapter: NavigationAdapter? = null
    private var pagerAdapter: FragmentPagerAdapter<AppFragment<*>>? = null

    override fun getLayoutId(): Int {
        return R.layout.home_activity
    }

    override fun initView() {
        navigationAdapter = NavigationAdapter(this).apply {
            addItem(NavigationAdapter.MenuItem(getString(R.string.home_nav_index),
                ContextCompat.getDrawable(this@HomeActivity, R.drawable.home_home_selector)))
            addItem(NavigationAdapter.MenuItem(getString(R.string.home_nav_found),
                ContextCompat.getDrawable(this@HomeActivity, R.drawable.home_found_selector)))
            addItem(NavigationAdapter.MenuItem(getString(R.string.home_nav_message),
                ContextCompat.getDrawable(this@HomeActivity, R.drawable.home_message_selector)))
            addItem(NavigationAdapter.MenuItem(getString(R.string.home_nav_me),
                ContextCompat.getDrawable(this@HomeActivity, R.drawable.home_me_selector)))
            setOnNavigationListener(this@HomeActivity)
        }

        mCooperation = JniCooperation()
        val hosts = MainService.getIPv4s()
        val host = hosts[0]
        mCooperation?.initNative(host)

        mCooperation?.registerListener(object : CooperationListener {
            override fun onConnectChanged(result: Int, reason: String) {
                Log.d(TAG, "Connection changed: result $result, reason: $reason")
            }

            override fun onAsyncRpcResult(type: Int, response: String) {
                Log.d(TAG, "Async RPC result: type $type, response: $response")
                when (type) {
                    JniCooperation.RPC_APPLY_LOGIN -> {
                        //type 1000, response: {"auth":"thatsgood","name":"10.8.11.52"}
                        if (response.isNullOrEmpty()) {
                            Log.e(TAG, "FAIL to connect...")
                            // TODO: show connection failed diaglog.
                            return
                        }
                        val jsonObject = JSONObject(response)
                        val auth = jsonObject.getString("auth")
                        val name = jsonObject.getString("name")
                        if (auth.isNullOrEmpty()) {
                            Log.e(TAG, "FAIL to connect...")
                            // TODO: show connection failed diaglog.
                            return
                        }

                        Log.d(TAG, "Login success: name $name, auth: $auth")
                        this@HomeActivity.viewModel.updateInfo(auth, name)
                        start(this@HomeActivity, HomeFragment::class.java)
                    }
                    JniCooperation.RPC_APPLY_PROJECTION -> {
                        // start the vnc service after send the projection rpc success.
                        switchVncServer()
                    }
                    JniCooperation.RPC_APPLY_PROJECTION_RESULT -> {
                    }
                    JniCooperation.RPC_APPLY_PROJECTION_STOP -> {
                        switchVncServer()
                    }
                }
            }
        })

        mMainServiceBroadcastReceiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                if (MainService.ACTION_START == intent.action) {
                    if (intent.getBooleanExtra(MainService.EXTRA_REQUEST_SUCCESS, false)) {
                        // was a successful START requested by anyone (but sent by MainService, as the receiver is not exported!)
                        Log.d(TAG, "got MainService started success event")
                        onServerStarted()
                    } else {
                        // was a failed START requested by anyone (but sent by MainService, as the receiver is not exported!)
                        Log.d(TAG, "got MainService started fail event")
                        onServerFailed()
                    }
                }

                if (MainService.ACTION_STOP == intent.action && (intent.getBooleanExtra(
                        MainService.EXTRA_REQUEST_SUCCESS,
                        true
                    ))
                ) {
                    // was a successful STOP requested by anyone (but sent by MainService, as the receiver is not exported!)
                    // or a STOP without any extras
                    Log.d(TAG, "got MainService stopped event")
                    onServerStopped()
                }
            }
        }
        val filter = IntentFilter()
        filter.addAction(MainService.ACTION_START)
        filter.addAction(MainService.ACTION_STOP)
        filter.addAction(MainService.ACTION_CONNECT_REVERSE)
        filter.addAction(MainService.ACTION_CONNECT_REPEATER)
        // register the receiver as NOT_EXPORTED so it only receives broadcasts sent by MainService,
        // not a malicious fake broadcaster like
        // `adb shell am broadcast -a net.christianbeier.droidvnc_ng.ACTION_STOP --ez net.christianbeier.droidvnc_ng.EXTRA_REQUEST_SUCCESS true`
        // for instance
        // androidx 1.8.0 以上高版本，SDK34
        // ContextCompat.registerReceiver(this, mMainServiceBroadcastReceiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED)
        registerReceiver(mMainServiceBroadcastReceiver, filter)

        // setup UI initial state
        if (MainService.isServerActive()) {
            Log.d(TAG, "Found server to be started")
            onServerStarted()
        } else {
            Log.d(TAG, "Found server to be stopped")
            onServerStopped()
        }
    }

    override fun initData() {
        pagerAdapter = FragmentPagerAdapter<AppFragment<*>>(this).apply {
            addFragment(HomeFragment.newInstance())
            addFragment(FirstFragment.newInstance())
            addFragment(MessageFragment.newInstance())
            addFragment(MineFragment.newInstance())
            viewPager?.adapter = this
        }
        onNewIntent(intent)

        val deviceName = Utils.getDeviceName(this)
        deviceName?.let { mCooperation?.setDeviceName(it) }

        viewModel = ViewModelProvider(this).get(SharedViewModel::class.java)
        viewModel.action().observe(this, Observer {
            Log.d(TAG, "will do the action: $it")
            when (it) {
                JniCooperation.ACTION_PROJECTION_START -> {
                    deviceName?.let { it -> mCooperation?.sendProjection(it, 5900) }
                }

                JniCooperation.ACTION_PROJECTION_STOP -> {
                    deviceName?.let { it -> mCooperation?.stopProjection(it) }
                }
            }
        })
    }

    override fun onNewIntent(intent: Intent?) {
        super.onNewIntent(intent)
        pagerAdapter?.let {
            switchFragment(it.getFragmentIndex(getSerializable(INTENT_KEY_IN_FRAGMENT_CLASS)))
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        viewPager?.let {
            // 保存当前 Fragment 索引位置
            outState.putInt(INTENT_KEY_IN_FRAGMENT_INDEX, it.currentItem)
        }
    }

    override fun onRestoreInstanceState(savedInstanceState: Bundle) {
        super.onRestoreInstanceState(savedInstanceState)
        // 恢复当前 Fragment 索引位置
        switchFragment(savedInstanceState.getInt(INTENT_KEY_IN_FRAGMENT_INDEX))
    }

    private fun switchFragment(fragmentIndex: Int) {
        if (fragmentIndex == -1) {
            return
        }
        when (fragmentIndex) {
            0, 1, 2, 3 -> {
                viewPager?.currentItem = fragmentIndex
                navigationAdapter?.setSelectedPosition(fragmentIndex)
            }
        }
    }

    /**
     * [NavigationAdapter.OnNavigationListener]
     */
    override fun onNavigationItemSelected(position: Int): Boolean {
        return when (position) {
            0, 1, 2, 3 -> {
                viewPager?.currentItem = position
                true
            }
            else -> false
        }
    }

    override fun createStatusBarConfig(): ImmersionBar {
        return super.createStatusBarConfig() // 指定导航栏背景颜色
            .navigationBarColor(R.color.white)
    }

    override fun onBackPressed() {
        if (!DoubleClickHelper.isOnDoubleClick()) {
            toast(R.string.home_exit_hint)
            return
        }

        // 移动到上一个任务栈，避免侧滑引起的不良反应
        moveTaskToBack(false)
        postDelayed({
            // 进行内存优化，销毁掉所有的界面
            ActivityManager.getInstance().finishAllActivities()
        }, 300)
    }

    @SuppressLint("SetTextI18n")
    override fun onResume() {
        super.onResume()

        /*
            Update Input permission display.
         */
        val inputStatus = findViewById<TextView>(R.id.permission_status_input)
        if (InputService.isConnected()) {
            inputStatus?.setText(R.string.main_activity_granted)
            inputStatus?.setTextColor(getColor(R.color.granted))
        } else {
            inputStatus?.setText(R.string.main_activity_denied)
            inputStatus?.setTextColor(getColor(R.color.denied))
        }


        /*
            Update Notification permission display. Only show on >= Android 13.
         */
        if (Build.VERSION.SDK_INT >= 33) {
            val notificationStatus = findViewById<TextView>(R.id.permission_status_notification)
            if (checkSelfPermission(Permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED) {
                notificationStatus?.setText(R.string.main_activity_granted)
                notificationStatus?.setTextColor(getColor(R.color.granted))
            } else {
                notificationStatus?.setText(R.string.main_activity_denied)
                notificationStatus?.setTextColor(getColor(R.color.denied))
            }
            notificationStatus?.setOnClickListener { view: View? ->
                val intent = Intent(
                    Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse(
                        "package:$packageName"
                    )
                )
                startActivity(intent)
            }
        } else {
            findViewById<View>(R.id.permission_row_notification)?.visibility = View.GONE
        }


        /*
           Update Screen Capturing permission display.
        */
        val screenCapturingStatus = findViewById<TextView>(R.id.permission_status_screen_capturing)
        if (MediaProjectionService.isMediaProjectionEnabled()) {
            screenCapturingStatus?.setText(R.string.main_activity_granted)
            screenCapturingStatus?.setTextColor(getColor(R.color.granted))
        }
        if (!MediaProjectionService.isMediaProjectionEnabled()) {
            screenCapturingStatus?.setText(R.string.main_activity_denied)
            screenCapturingStatus?.setTextColor(getColor(R.color.denied))
        }
        if (!MediaProjectionService.isMediaProjectionEnabled() && InputService.isTakingScreenShots()) {
            screenCapturingStatus?.setText(R.string.main_activity_fallback)
            screenCapturingStatus?.setTextColor(getColor(R.color.fallback))
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        viewPager?.adapter = null

        navigationAdapter?.setOnNavigationListener(null)
        unregisterReceiver(mMainServiceBroadcastReceiver)
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (resultCode == RESULT_OK) {
            val resultCodeFromScan = data?.getStringExtra("SCAN_RESULT")
            Log.d(TAG, "扫描结果传回 HomeActivity: $resultCodeFromScan")

            resultCodeFromScan?.let {
                // 先进行 base64 解码
                val decodedBytes = Base64.decode(it, Base64.DEFAULT)
                val decodedString = String(decodedBytes)

                // 按照"&"分割字符串
                val parameters = decodedString.split("&")
                if (parameters.size == 3) {
                    // 创建一个存储解析结果的 Map
                    val resultMap = mutableMapOf<String, String>()

                    // 遍历参数并进行分割
                    for (parameter in parameters) {
                        val keyValue = parameter.split("=")
                        if (keyValue.size == 2) {
                            resultMap[keyValue[0]] = keyValue[1]
                        } else {
                            Log.e(TAG, "解析出错，参数格式不正确：$parameter")
                        }
                    }

                    // 提取出 ip、port 和 pin
                    val ip = resultMap["host"]
                    val portString = resultMap["port"]
                    val pin = resultMap["pin"]

                    // 将 port 转换为 Int，如果失败默认为 0
                    val port = portString?.toIntOrNull() ?: 0

                    if (ip != null && pin != null) {
                        // 调用连接方法
                        connectCooperation(ip, port, pin)
                    } else {
                        Log.e(TAG, "解析出错，参数不完整：host=$ip, port=$portString, pin=$pin")
                    }
                } else {
                    Log.e(TAG, "解析出错，参数数量不匹配：$decodedString")
                }
            }
        }
    }

    fun connectCooperation(ip: String, port: Int, pin: String) {
        mCooperation?.connectRemote(ip, port, pin)
    }

    fun switchVncServer() {
        val prefs = PreferenceManager.getDefaultSharedPreferences(this)
        mDefaults = Defaults(this)

        val intent = Intent(this@HomeActivity, MainService::class.java)
        intent.putExtra(
            MainService.EXTRA_PORT,
            prefs.getInt(Constants.PREFS_KEY_SETTINGS_PORT, mDefaults!!.port)
        )
        intent.putExtra(
            MainService.EXTRA_PASSWORD,
            prefs.getString(Constants.PREFS_KEY_SETTINGS_PASSWORD, mDefaults!!.password)
        )
        intent.putExtra(
            MainService.EXTRA_FILE_TRANSFER,
            prefs.getBoolean(
                Constants.PREFS_KEY_SETTINGS_FILE_TRANSFER,
                mDefaults!!.fileTransfer
            )
        )
        intent.putExtra(
            MainService.EXTRA_VIEW_ONLY,
            prefs.getBoolean(Constants.PREFS_KEY_SETTINGS_VIEW_ONLY, mDefaults!!.viewOnly)
        )
        intent.putExtra(
            MainService.EXTRA_SHOW_POINTERS,
            false // disable show pointer
//            prefs.getBoolean(
//                Constants.PREFS_KEY_SETTINGS_SHOW_POINTERS,
//                mDefaults!!.showPointers
//            )
        )
        intent.putExtra(
            MainService.EXTRA_SCALING,
            prefs.getFloat(Constants.PREFS_KEY_SETTINGS_SCALING, mDefaults!!.scaling)
        )
        intent.putExtra(
            MainService.EXTRA_ACCESS_KEY,
            prefs.getString(Constants.PREFS_KEY_SETTINGS_ACCESS_KEY, mDefaults!!.accessKey)
        )
        if (mIsMainServiceRunning) {
            intent.setAction(MainService.ACTION_STOP)
        } else {
            intent.setAction(MainService.ACTION_START)
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent)
        } else {
            startService(intent)
        }
    }

    private fun onServerFailed() {
    }

    private fun onServerStarted() {


        mIsMainServiceRunning = true
    }

    private fun onServerStopped() {


        mIsMainServiceRunning = false
    }
}